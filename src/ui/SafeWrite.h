#pragma once

// ---------------------------------------------------------------------------
//  图片的安全落盘
//
//  字节 / 文本的原子写见 core/SafeWrite.h（core 与 ui 共用那一份）。
//  这里只补图片：让 QImageWriter 直接写到 QSaveFile 上，省一次内存拷贝。
// ---------------------------------------------------------------------------

#include "core/SafeWrite.h"

#include <QString>

class QImage;

namespace csm {
namespace SafeWrite {

/// 原子写一张图片，格式由扩展名决定（PNG 最常用）
bool image(const QImage& image, const QString& path, QString* error);

} // namespace SafeWrite
} // namespace csm
