#pragma once

// ---------------------------------------------------------------------------
//  安全落盘
//
//  写用户文件（.csm、恢复副本、导出的 CSV）时不能直接 QFile::open(WriteOnly)：
//  那会先把目标截成 0 字节，写到一半失败（磁盘满、断电、进程被杀）就只剩半截，
//  原来那份也没了。这里统一走 QSaveFile：先写同目录的临时文件，成功后原子替换。
//
//  只依赖 QtCore，所以 core 与 ui 都能用；图片导出见 ui/SafeWrite.h。
// ---------------------------------------------------------------------------

#include <QByteArray>
#include <QString>

namespace csm {
namespace SafeWrite {

/// 原子写一段字节；失败时把原因写进 error（可为空指针）
bool bytes(const QString& path, const QByteArray& data, QString* error);

/// 便捷版：按 UTF-8 写字符串，不做换行转换
bool text(const QString& path, const QString& content, QString* error);

} // namespace SafeWrite
} // namespace csm
