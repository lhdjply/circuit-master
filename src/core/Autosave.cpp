#include "core/Autosave.h"

#include "core/Language.h"
#include "core/SafeWrite.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QSettings>
#include <QStandardPaths>

#include <algorithm>

namespace csm {
namespace Autosave {

namespace {

constexpr int kDefaultInterval = 10;
constexpr int kMinInterval = 1;
constexpr int kMaxInterval = 120;

QString fileNameFor(const QString& id)
{
    return id.isEmpty() ? QString() : QStringLiteral("%1.json").arg(id);
}

} // namespace

bool enabled()
{
    QSettings settings;
    return settings.value(QStringLiteral("autosave/enabled"), true).toBool();
}

void setEnabled(bool value)
{
    QSettings settings;
    settings.setValue(QStringLiteral("autosave/enabled"), value);
}

int intervalMinutes()
{
    QSettings settings;
    const int minutes = settings.value(QStringLiteral("autosave/interval"), kDefaultInterval).toInt();
    return std::clamp(minutes, kMinInterval, kMaxInterval);
}

void setIntervalMinutes(int minutes)
{
    QSettings settings;
    settings.setValue(QStringLiteral("autosave/interval"),
                      std::clamp(minutes, kMinInterval, kMaxInterval));
}

bool recoveryEnabled()
{
    QSettings settings;
    return settings.value(QStringLiteral("autosave/recovery"), true).toBool();
}

void setRecoveryEnabled(bool value)
{
    QSettings settings;
    settings.setValue(QStringLiteral("autosave/recovery"), value);
}

int recoveryIntervalMinutes()
{
    QSettings settings;
    const int minutes = settings.value(QStringLiteral("autosave/recoveryInterval"), 2).toInt();
    return std::clamp(minutes, 1, 60);
}

void setRecoveryIntervalMinutes(int minutes)
{
    QSettings settings;
    settings.setValue(QStringLiteral("autosave/recoveryInterval"),
                      std::clamp(minutes, 1, 60));
}

QString directory()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    const QString path = QDir(base.isEmpty() ? QDir::homePath() : base)
                             .filePath(QStringLiteral("recovery"));
    QDir().mkpath(path);
    return path;
}

bool write(const Record& record, QString* error)
{
    // 成功时也要清空：调用方可能复用同一个字符串，留着上次的内容会误导
    if (error)
        error->clear();
    const QString name = fileNameFor(record.id);
    if (name.isEmpty()) {
        if (error)
            *error = CSM_TR("The recovery record has no id.");
        return false;
    }

    QJsonObject root;
    root.insert(QStringLiteral("id"), record.id);
    root.insert(QStringLiteral("original"), record.originalPath);
    root.insert(QStringLiteral("savedAt"), record.savedAt.toString(Qt::ISODate));
    root.insert(QStringLiteral("circuit"), record.circuit);

    // 走 SafeWrite（QSaveFile）：写副本的过程中崩溃，不能把上一次的副本也截成
    // 半截 —— 那样「崩溃恢复」正好在最需要它的时候失效。
    const QByteArray data = QJsonDocument(root).toJson(QJsonDocument::Compact);
    return SafeWrite::bytes(QDir(directory()).filePath(name), data, error);
}

QVector<Record> pending()
{
    QVector<Record> records;
    QDir dir(directory());
    for (const QString& name : dir.entryList({QStringLiteral("*.json")}, QDir::Files)) {
        QFile file(dir.filePath(name));
        if (!file.open(QIODevice::ReadOnly))
            continue;
        const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
        Record record;
        record.id = root.value(QStringLiteral("id")).toString();
        record.originalPath = root.value(QStringLiteral("original")).toString();
        record.savedAt = QDateTime::fromString(root.value(QStringLiteral("savedAt")).toString(),
                                               Qt::ISODate);
        record.circuit = root.value(QStringLiteral("circuit")).toObject();
        if (record.id.isEmpty() || record.circuit.isEmpty())
            continue;
        records.append(record);
    }
    std::sort(records.begin(), records.end(), [](const Record& a, const Record& b) {
        return a.savedAt > b.savedAt;
    });
    return records;
}

bool read(const QString& id, Record* out)
{
    if (!out || id.isEmpty())
        return false;
    QFile file(QDir(directory()).filePath(fileNameFor(id)));
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    out->id = root.value(QStringLiteral("id")).toString();
    out->originalPath = root.value(QStringLiteral("original")).toString();
    out->savedAt = QDateTime::fromString(root.value(QStringLiteral("savedAt")).toString(),
                                         Qt::ISODate);
    out->circuit = root.value(QStringLiteral("circuit")).toObject();
    return !out->id.isEmpty() && !out->circuit.isEmpty();
}

void remove(const QString& id)
{
    const QString name = fileNameFor(id);
    if (name.isEmpty())
        return;
    QFile::remove(QDir(directory()).filePath(name));
}

void clearAll()
{
    QDir dir(directory());
    for (const QString& name : dir.entryList({QStringLiteral("*.json")}, QDir::Files))
        QFile::remove(dir.filePath(name));
}

} // namespace Autosave
} // namespace csm
