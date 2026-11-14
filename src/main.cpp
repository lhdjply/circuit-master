#include "csm_version.h"
#include "ui/MainWindow.h"

#include <QApplication>

#include <cstdio>
#include <QFont>
#include <QIcon>
#include <QPalette>
#include <QStyleFactory>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("电路仿真大师"));
    QApplication::setApplicationVersion(QStringLiteral(CSM_VERSION_NUMBER));
    QApplication::setOrganizationName(QStringLiteral("CircuitMaster"));

    // --version / -v 只打印版本，不启动界面（需要 QApplication 才能用 arguments()）
    const QStringList arguments = QCoreApplication::arguments();
    if (arguments.contains(QStringLiteral("--version"))
        || arguments.contains(QStringLiteral("-v"))) {
        std::printf("电路仿真大师 %s\n", CSM_VERSION_STRING);
        return 0;
    }

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
