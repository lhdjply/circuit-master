#pragma once

// ---------------------------------------------------------------------------
//  单实例
//
//  同一个用户只跑一个进程。第二次启动（例如双击另一个 .csm）时，把要打开的
//  文件通过本地套接字发给已经在跑的那个进程，然后自己退出 —— 文件在新标签页
//  里打开，而不是再开一个窗口。
//
//  为什么用 QLocalServer / QLocalSocket：
//   · Unix 下是 /tmp 里的套接字文件、Windows 下是命名管道，不用额外依赖；
//   · 名字里带上用户名，多用户共用一台机器时互不干扰。
//
//  上一个实例崩溃时套接字文件可能残留，listen 之前先 removeServer() 清掉。
// ---------------------------------------------------------------------------

#include <QObject>
#include <QString>
#include <QStringList>

class QLocalServer;

namespace csm {

class SingleInstance : public QObject {
    Q_OBJECT
public:
    /// key 决定套接字名字；同一台机器上同名的进程互相排斥
    explicit SingleInstance(const QString& key, QObject* parent = nullptr);
    ~SingleInstance() override;

    /// 加锁。返回 true 表示本进程就是唯一的那个，继续跑；
    /// 返回 false 表示已经有实例在跑 —— 要打开的文件已经转发过去，本进程应当退出。
    bool promote(const QStringList& files);

    /// 套接字名字（带用户名），测试里用来确认两个对象用的是同一个
    static QString socketName(const QString& key);

    /// 本进程是否真的在监听。监听失败时 promote() 仍返回 true（宁可多开一个
    /// 窗口也不能起不来），调用方可以据此判断「单实例」到底生效没有
    bool listening() const { return m_server != nullptr; }

signals:
    /// 另一个实例发来了「打开这些文件」的请求
    void filesReceived(const QStringList& files);

private:
    void acceptConnection();

    QString m_socketName;
    QLocalServer* m_server = nullptr;
};

} // namespace csm
