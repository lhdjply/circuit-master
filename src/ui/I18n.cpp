#include "ui/I18n.h"

#include "core/Language.h"

#include <QCoreApplication>
#include <QDir>
#include <QLibraryInfo>
#include <QLocale>
#include <QTranslator>

namespace csm {

static bool g_translationsLoaded = false;
static QTranslator* g_qtTranslator = nullptr;

bool qtTranslationsLoaded()
{
    return g_translationsLoaded;
}

void installQtTranslations(QCoreApplication* app)
{
    if (!app)
        return;

    // Qt 自带控件（输入框右键菜单、标准按钮）的语言跟着界面语言走：
    // 英文界面下装中文翻译就成了「英文菜单 + 中文右键菜单」的怪组合
    const bool chineseUi = (csm::language() == csm::Language::Chinese);
    QLocale::setDefault(chineseUi ? QLocale(QLocale::Chinese, QLocale::China)
                                  : QLocale(QLocale::English, QLocale::UnitedStates));

    // 换语言时先把上一次装的卸掉，否则切回英文后右键菜单还是中文
    if (g_qtTranslator) {
        app->removeTranslator(g_qtTranslator);
        delete g_qtTranslator;
        g_qtTranslator = nullptr;
    }
    g_translationsLoaded = false;
    if (!chineseUi)
        return;   // 英文是 Qt 的默认语言，不用装翻译

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
            g_qtTranslator = translator;
            g_translationsLoaded = true;
            return;
        }
        delete translator;
    }
}

} // namespace csm
