#pragma once

// ---------------------------------------------------------------------------
//  界面语言
//
//  源码里的文案一律是英文，中文放在 resources/translations/*.ts（Qt Linguist
//  格式），构建时用 lrelease 编成 .qm 并嵌进资源。运行时按设置装/卸 QTranslator
//  即可切换。
//
//  用 CSM_TR("Open File") 取文案 —— 它等价于 QCoreApplication::translate()，
//  上下文固定为 "csm"，这样 .ts 里的条目都归在同一个 context 下。
//  不要用它包 JSON 键、objectName 之类的数据。
// ---------------------------------------------------------------------------

#include <QCoreApplication>
#include <QString>

namespace csm {

enum class Language {
    Chinese,
    English,
};

/// 启动时读设置并装上对应的翻译
void loadLanguage();
Language language();

/// 存盘（界面重建由调用方负责）
void saveLanguage(Language value);

/// 语言在界面上的名字
QString languageDisplayName(Language value);

} // namespace csm

/// 取当前语言的文案。源码里写英文，翻译在 .ts 里。
#define CSM_TR(text) QCoreApplication::translate("csm", text)
