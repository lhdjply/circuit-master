#include "csm_version.h"
#include "ui/MainWindow.h"
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

/// 第一个不是选项的参数：双击 .csm 时由文件关联传进来。
/// Windows 的路径可能带引号，这里顺手去掉。
QString firstFileArgument(int argc, char* argv[])
{
    for (int i = 1; i < argc; ++i) {
        const char* raw = argv[i];
        if (!raw || raw[0] == '-')
            continue;
        QString value = QString::fromLocal8Bit(raw).trimmed();
        if (value.size() >= 2 && value.startsWith(QLatin1Char('"'))
            && value.endsWith(QLatin1Char('"')))
            value = value.mid(1, value.size() - 2);
        if (!value.isEmpty())
            return value;
    }
    return QString();
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

    if (QStyle* fusion = QStyleFactory::create(QStringLiteral("Fusion")))
        QApplication::setStyle(fusion);

    QPalette palette = app.palette();
    palette.setColor(QPalette::Window, QColor(0xf5, 0xf7, 0xfa));
    palette.setColor(QPalette::WindowText, QColor(0x1e, 0x29, 0x3b));
    palette.setColor(QPalette::Base, Qt::GlobalColor::white);
    palette.setColor(QPalette::Highlight, QColor(0x25, 0x63, 0xeb));
    palette.setColor(QPalette::HighlightedText, Qt::white);
    palette.setColor(QPalette::Button, QColor(0xf1, 0xf5, 0xf9));
    palette.setColor(QPalette::ButtonText, QColor(0x1e, 0x29, 0x3b));
    app.setPalette(palette);

    QApplication::setWindowIcon(QIcon(QStringLiteral(":/icons/icon.svg")));

    QFont font = app.font();
    font.setPointSizeF(10.0);
    app.setFont(font);

    // 上次更新装完后没删掉的安装包，这里补删
    csm::UpdateDialog::cleanupPendingInstaller();

    csm::MainWindow window;
    window.show();

    // 双击 .csm（文件关联）或 `CircuitMaster 电路.csm` 时直接打开
    const QString fileToOpen = firstFileArgument(argc, argv);
    if (!fileToOpen.isEmpty() && !window.openPath(fileToOpen))
        return 1;

    return app.exec();
}
