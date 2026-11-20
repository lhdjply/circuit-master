#include "csm_version.h"
#include "ui/I18n.h"
#include "ui/Theme.h"
#include "ui/MainWindow.h"
#include "ui/SingleInstance.h"
#include "ui/UpdateDialog.h"

#include <QApplication>

#include <QString>

#include <cstdio>
#include <cstring>

#include <QFont>
#include <QIcon>
#include <QPalette>
#include <QStyleFactory>

namespace {

/// 直接比较 argv，不依赖 QCoreApplication。
/// 命令行参数必须在创建 QApplication 之前处理：没有显示器（CI、ssh、容器）时
/// 创建 GUI 应用会直接因为找不到平台插件而 abort，--version 也就无从谈起。
bool hasArgument(int argc, char* argv[], const char* name)
{
    for (int i = 1; i < argc; ++i) {
        if (argv[i] && std::strcmp(argv[i], name) == 0)
            return true;
    }
    return false;
}

/// 所有不是选项的参数：双击 .csm 时由文件关联传进来。
/// Windows 的路径可能带引号，这里顺手去掉。
QStringList fileArguments(int argc, char* argv[])
{
    QStringList files;
    for (int i = 1; i < argc; ++i) {
        const char* raw = argv[i];
        if (!raw || raw[0] == '-')
            continue;
        QString value = QString::fromLocal8Bit(raw).trimmed();
        if (value.size() >= 2 && value.startsWith(QLatin1Char('"'))
            && value.endsWith(QLatin1Char('"')))
            value = value.mid(1, value.size() - 2);
        if (!value.isEmpty())
            files.append(value);
    }
    return files;
}

} // namespace

int main(int argc, char* argv[])
{
    if (hasArgument(argc, argv, "--version") || hasArgument(argc, argv, "-v")) {
        std::printf("电路仿真大师 %s\n", CSM_VERSION_STRING);
        return 0;
    }
    if (hasArgument(argc, argv, "--help") || hasArgument(argc, argv, "-h")) {
        std::printf("电路仿真大师 %s\n\n", CSM_VERSION_STRING);
        std::printf("用法: CircuitMaster [选项] [文件.csm]\n\n");
        std::printf("选项:\n");
        std::printf("  -h, --help     显示本帮助并退出\n");
        std::printf("  -v, --version  显示版本号并退出\n");
        return 0;
    }

    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("电路仿真大师"));
    QApplication::setApplicationVersion(QStringLiteral(CSM_VERSION_NUMBER));
    QApplication::setOrganizationName(QStringLiteral("CircuitMaster"));

    // Qt 自带控件的中文（输入框右键菜单、标准按钮等）
    csm::installQtTranslations(&app);

    // Fusion + 关掉禁用文字的浮雕（深色下那道浮雕会变成重影）
    QApplication::setStyle(csm::Theme::appStyle());

    // 浅色 / 深色：模式存在 QSettings 里，这里读回来再装调色板
    csm::Theme::load();
    csm::Theme::apply(app);

    QApplication::setWindowIcon(QIcon(QStringLiteral(":/icons/icon.svg")));

    QFont font = app.font();
    font.setPointSizeF(10.0);
    app.setFont(font);

    // 上次更新装完后没删掉的安装包，这里补删
    csm::UpdateDialog::cleanupPendingInstaller();

    const QStringList startupFiles = fileArguments(argc, argv);

    // 单实例：已经有窗口在跑的话，把要打开的文件发过去，自己退出。
    // 这样双击第二个 .csm 会在原窗口里开成新标签，而不是再起一个进程。
    csm::SingleInstance instance(QStringLiteral("circuit-master"), &app);
    if (!instance.promote(startupFiles))
        return 0;

    csm::MainWindow window;
    window.show();

    // 运行中收到另一个实例的请求（用户又双击了一个文件）
    QObject::connect(&instance, &csm::SingleInstance::filesReceived, &window,
                     [&window](const QStringList& files) {
                         for (const QString& file : files)
                             window.openPath(file);
                         window.raise();
                         window.activateWindow();
                     });

    // 双击 .csm（文件关联）或 `CircuitMaster 电路.csm` 时直接打开
    for (const QString& file : startupFiles) {
        if (!window.openPath(file))
            return 1;
    }

    return app.exec();
}
