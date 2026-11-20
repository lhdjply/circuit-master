#include "ui/SingleInstance.h"

#include <QEventLoop>
#include <QLocalServer>
#include <QLocalSocket>
#include <QRegularExpression>
#include <QTimer>

namespace csm {

namespace {

/// 连接已有实例时的重试次数与间隔：刚启动的进程可能还没 listen 好，
/// 但也不值得等太久 —— 一共约 600 ms。
constexpr int kConnectAttempts = 6;
constexpr int kConnectIntervalMs = 100;
/// 写完数据后留给对端「accept + 读走」的时间，再断开连接
constexpr int kFlushGraceMs = 120;

} // namespace

SingleInstance::SingleInstance(const QString& key, QObject* parent)
    : QObject(parent)
{
    m_socketName = socketName(key);
}

SingleInstance::~SingleInstance()
{
    if (!m_server)
        return;
    // 先断开所有回调：close() 会掐断挂着的连接，socket 在析构时可能补发
    // disconnected —— 那时本对象已经拆到一半，进去就是野指针
    m_server->disconnect(this);
    for (QLocalSocket* socket : m_server->findChildren<QLocalSocket*>())
        socket->disconnect(this);
    m_server->close();
    QLocalServer::removeServer(m_socketName);
    delete m_server;
    m_server = nullptr;
}

QString SingleInstance::socketName(const QString& key)
{
    // 名字里带用户名：多用户共用一台机器时互不干扰。
    // 套接字名字里不能有路径分隔符之类，非法字符统一换成下划线。
    QString user = qEnvironmentVariable("USER");
    if (user.isEmpty())
        user = qEnvironmentVariable("USERNAME");
    if (user.isEmpty())
        user = QStringLiteral("default");
    user.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_.-]")),
                 QStringLiteral("_"));

    QString name = key;
    name.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_.-]")),
                 QStringLiteral("_"));
    return name + QLatin1Char('-') + user;
}

bool SingleInstance::promote(const QStringList& files)
{
    // 1) 先试着连已经在跑的那个实例
    for (int attempt = 0; attempt < kConnectAttempts; ++attempt) {
        QLocalSocket socket;
        socket.connectToServer(m_socketName);
        if (socket.waitForConnected(kConnectIntervalMs)) {
            if (!files.isEmpty()) {
                // 一行一个路径，UTF-8 编码；以换行结尾方便对方判断「发完了」
                const QByteArray payload = files.join(QLatin1Char('\n')).toUtf8()
                    + QByteArrayLiteral("\n");
                socket.write(payload);
                socket.flush();
                socket.waitForBytesWritten(1000);
            }
            // 不能写完就立刻断开：Windows 的命名管道在 DisconnectNamedPipe 时
            // 会把对端**还没读走**的数据丢掉。这里跑一小段事件循环（真实运行时
            // 对端是另一个进程，它自己的事件循环在跑；同进程的测试也能借这段
            // 时间处理 newConnection），然后再优雅断开。
            {
                QEventLoop loop;
                QTimer::singleShot(kFlushGraceMs, &loop, &QEventLoop::quit);
                loop.exec();
            }
            socket.disconnectFromServer();
            if (socket.state() != QLocalSocket::UnconnectedState)
                socket.waitForDisconnected(500);
            return false; // 已有实例：转发完就退出
        }
    }

    // 2) 没有实例在跑：上一个进程崩溃时可能留下套接字文件，先清掉
    QLocalServer::removeServer(m_socketName);
    m_server = new QLocalServer(this);
    m_server->setSocketOptions(QLocalServer::UserAccessOption);
    if (!m_server->listen(m_socketName)) {
        // 监听失败（例如目录不可写）时不阻断启动：宁可多开一个窗口，
        // 也不能因为「单实例」这个便利功能让程序起不来
        delete m_server;
        m_server = nullptr;
        return true;
    }
    connect(m_server, &QLocalServer::newConnection, this, &SingleInstance::acceptConnection);
    return true;
}

void SingleInstance::acceptConnection()
{
    while (m_server && m_server->hasPendingConnections()) {
        QLocalSocket* socket = m_server->nextPendingConnection();
        if (!socket)
            continue;

        // 客户端写完就断开。这里的关键是：等我们拿到这个 socket 时，
        // 它可能**已经断开、甚至数据都已经到齐了** —— Windows 命名管道的
        // 时序和 Unix 套接字不同，很容易在挂信号之前就走完。
        // 所以挂上信号之后必须再补查一次当前状态，不能只靠信号。
        auto finish = [this, socket] {
            if (socket->property("csmHandled").toBool())
                return;
            socket->setProperty("csmHandled", true);

            const QStringList lines =
                QString::fromUtf8(socket->property("csmBuffer").toByteArray())
                    .split(QLatin1Char('\n'));
            QStringList files;
            for (const QString& line : lines) {
                const QString path = line.trimmed();
                if (!path.isEmpty())
                    files.append(path);
            }
            socket->deleteLater();
            if (!files.isEmpty())
                emit filesReceived(files);
        };

        auto drain = [socket] {
            socket->setProperty("csmBuffer",
                                socket->property("csmBuffer").toByteArray() + socket->readAll());
        };

        connect(socket, &QLocalSocket::readyRead, socket, drain);
        connect(socket, &QLocalSocket::disconnected, socket, finish);

        // 补查：挂信号之前可能已经读过数据，或者已经断开
        if (socket->bytesAvailable() > 0)
            drain();
        if (socket->state() == QLocalSocket::UnconnectedState)
            finish();
    }
}

} // namespace csm
