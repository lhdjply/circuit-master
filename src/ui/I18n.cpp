#include "ui/I18n.h"

#include <QCoreApplication>
#include <QDir>
#include <QLibraryInfo>
#include <QLocale>
#include <QTranslator>

namespace csm {

static bool g_translationsLoaded = false;

bool qtTranslationsLoaded()
{
    return g_translationsLoaded;
}

void installQtTranslations(QCoreApplication* app)
{
    if (!app)
        return;

    QLocale::setDefault(QLocale(QLocale::Chinese, QLocale::China));

    const QStringList dirs = {
        QLibraryInfo::path(QLibraryInfo::TranslationsPath),
        QCoreApplication::applicationDirPath() + QStringLiteral("/translations"),
#ifdef Q_OS_MACOS
        // .app 里更常见的放法是 Contents/Resources/translations，
        // 打包脚本往那里放时也能找到
        QCoreApplication::applicationDirPath() + QStringLiteral("/../Resources/translations"),
#endif
    };
    for (const QString& dir : dirs) {
        auto* translator = new QTranslator(app);
        if (translator->load(QStringLiteral("qtbase_zh_CN"), dir)) {
            app->installTranslator(translator);
            g_translationsLoaded = true;
            return;
        }
        delete translator;
    }
}

} // namespace csm
