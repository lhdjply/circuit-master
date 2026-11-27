#include "core/Language.h"

#include <QCoreApplication>
#include <QSettings>
#include <QTranslator>

namespace csm {

namespace {

Language g_language = Language::Chinese;
QTranslator* g_translator = nullptr;

/// 装上某个语言的 .qm；中文是内嵌资源，找不到时静默跳过（界面显示英文）
void install(Language value)
{
    if (g_translator) {
        QCoreApplication::removeTranslator(g_translator);
        delete g_translator;
        g_translator = nullptr;
    }
    if (value == Language::English)
        return; // 英文就是源码里那句，不需要翻译文件

    auto* translator = new QTranslator(QCoreApplication::instance());
    if (translator->load(QStringLiteral(":/i18n/circuit-master_zh_CN.qm"))) {
        QCoreApplication::installTranslator(translator);
        g_translator = translator;
    } else {
        delete translator;
    }
}

QString keyOf(Language value)
{
    return value == Language::English ? QStringLiteral("en") : QStringLiteral("zh");
}

Language fromKey(const QString& key)
{
    return key == QLatin1String("en") ? Language::English : Language::Chinese;
}

} // namespace

void loadLanguage()
{
    g_language = fromKey(QSettings().value(QStringLiteral("language/name"),
                                           QStringLiteral("zh")).toString());
    install(g_language);
}

Language language()
{
    return g_language;
}

void saveLanguage(Language value)
{
    g_language = value;
    QSettings().setValue(QStringLiteral("language/name"), keyOf(value));
    install(value);
}

QString languageDisplayName(Language value)
{
    // 各自用母语写，方便不认识的用户也找得到
    return value == Language::English ? QStringLiteral("English") : QStringLiteral("中文");
}

} // namespace csm
