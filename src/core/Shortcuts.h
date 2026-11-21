#pragma once

// ---------------------------------------------------------------------------
//  可改的快捷键（只负责存取，不懂 QKeySequence）
//
//  每个可改的命令有一个稳定 id（如 "file.new"），覆盖值存在 QSettings 的
//  shortcuts/<id> 里，内容是 QKeySequence 的 portable 文本（"Ctrl+Shift+N"）。
//  空串表示「不绑定」。界面层负责在 QKeySequence 与文本之间转换。
// ---------------------------------------------------------------------------

#include <QString>

namespace csm {

namespace Shortcuts {

/// 读出某个命令的覆盖值；没有记录时返回 fallbackText
QString load(const QString& id, const QString& fallbackText);

/// 保存覆盖值；与默认相同时删掉记录，保持配置文件干净
void save(const QString& id, const QString& sequenceText, const QString& fallbackText);

/// 清掉所有覆盖，全部回到默认
void clearAll();

} // namespace Shortcuts
} // namespace csm
