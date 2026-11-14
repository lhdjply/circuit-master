#include "csm_version.h"
#include "ui/MainWindow.h"

#include <QApplication>

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

    csm::MainWindow window;
    window.show();
    return app.exec();
}
