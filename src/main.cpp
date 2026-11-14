#include "ui/MainWindow.h"

#include <QApplication>
#include <QGuiApplication>
#include <QFont>
#include <QIcon>
#include <QPalette>
#include <QStyleFactory>

int main(int argc, char* argv[])
{
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("电路仿真大师"));
    QApplication::setApplicationVersion(QStringLiteral("0.1.0"));
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
