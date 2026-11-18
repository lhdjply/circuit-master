#include "ui/SingleInstance.h"

#include <QLocalServer>
#include <QLocalSocket>
#include <QRegularExpression>

namespace csm {

namespace {

/// 连接已有实例时的重试次数与间隔：刚启动的进程可能还没 listen 好，
/// 但也不值得等太久 —— 一共约 600 ms。
constexpr int kConnectAttempts = 6;
constexpr int kConnectIntervalMs = 100;

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
            socket.disconnectFromServer();
            if (socket.state() != QLocalSocket::UnconnectedState)
                socket.waitForDisconnected(300);
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

        // 对方写完就断开，这里按「收到的内容」拼起来，断开时统一处理：
        // 一条连接可能分几次到达
        connect(socket, &QLocalSocket::readyRead, socket, [socket] {
            socket->setProperty("buffer",
                                socket->property("buffer").toByteArray() + socket->readAll());
        });
        connect(socket, &QLocalSocket::disconnected, socket, [this, socket] {
            const QStringList lines =
                QString::fromUtf8(socket->property("buffer").toByteArray())
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
        });
    }
}

} // namespace csm
