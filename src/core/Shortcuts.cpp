#include "core/Shortcuts.h"

#include <QSettings>

namespace csm {
namespace Shortcuts {

namespace {
QString settingsKey(const QString& id)
{
    return QStringLiteral("shortcuts/%1").arg(id);
}
} // namespace

QString load(const QString& id, const QString& fallbackText)
{
    QSettings settings;
    const QString key = settingsKey(id);
    if (!settings.contains(key))
        return fallbackText;
    return settings.value(key).toString();   // 可能是空串（= 不绑定）
}

void save(const QString& id, const QString& sequenceText, const QString& fallbackText)
{
    QSettings settings;
    const QString key = settingsKey(id);
    if (sequenceText == fallbackText) {
        settings.remove(key);    // 与默认一致就不留记录
        return;
    }
    settings.setValue(key, sequenceText);
}

void clearAll()
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("shortcuts"));
    settings.remove(QString());
    settings.endGroup();
}

} // namespace Shortcuts
} // namespace csm
