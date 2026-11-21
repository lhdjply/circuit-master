#pragma once

// ---------------------------------------------------------------------------
//  自动保存 / 崩溃恢复
//
//  定时把「改过但还没保存」的图纸写一份到应用数据目录下的 recovery/，
//  正常退出或手动保存后删掉。下次启动时如果还留着记录，说明上次没退干净，
//  由主窗口问用户要不要恢复。
//
//  这里只管存取，不碰界面；开关与间隔存在 QSettings 的 autosave/ 下。
// ---------------------------------------------------------------------------

#include <QDateTime>
#include <QJsonObject>
#include <QString>
#include <QVector>

namespace csm {

namespace Autosave {

/// 是否启用（默认开）
bool enabled();
void setEnabled(bool enabled);

/// 间隔，单位分钟（默认 10，夹在 1..120）
int intervalMinutes();
void setIntervalMinutes(int minutes);

/// 崩溃恢复：要不要定时写恢复副本、并在启动时提示恢复（默认开）。
/// 关掉之后既不再写副本，启动时也不再询问，残留副本直接丢弃。
bool recoveryEnabled();
void setRecoveryEnabled(bool enabled);

/// 崩溃恢复的间隔，单位分钟（默认 2，夹在 1..60）。
/// 和「自动保存」的间隔分开：自动保存可以设得很疏，崩溃保护得勤一些。
int recoveryIntervalMinutes();
void setRecoveryIntervalMinutes(int minutes);

/// 恢复文件所在目录（不存在时会创建）
QString directory();

/// 一份待恢复的记录
struct Record {
    QString id;             // 文档的稳定标识，用作文文件名
    QString originalPath;   // 原文件路径；未保存过的新图纸为空
    QDateTime savedAt;
    QJsonObject circuit;
};

/// 写一份记录（覆盖同 id 的旧记录）
bool write(const Record& record);

/// 列出所有可恢复记录，按保存时间从新到旧
QVector<Record> pending();

/// 读一份记录
bool read(const QString& id, Record* out);

/// 删掉某份记录
void remove(const QString& id);

/// 删除全部记录（正常退出时调用）
void clearAll();

} // namespace Autosave
} // namespace csm
