#pragma once

// ---------------------------------------------------------------------------
//  Qt 自带控件的中文
//
//  程序自己的文案是中文硬编码，但 Qt 提供的标准控件（输入框右键菜单、标准按钮、
//  文件对话框等）默认是英文，需要加载 qtbase_zh_CN.qm 才会跟着变中文。
//  .qm 的位置因平台而异：Linux 在 Qt 的 translations/ 下，Windows 由
//  windeployqt 放到 exe 同级的 translations/，便携包靠 qt.conf 的 Prefix 指过去。
//  找不到时静默跳过 —— 只是这些控件显示英文，不影响程序本身。
// ---------------------------------------------------------------------------

class QCoreApplication;

namespace csm {

/// 装上 Qt 自带的中文翻译（可重复调用，重复装不会叠加）
void installQtTranslations(QCoreApplication* app);

/// 上一次 installQtTranslations() 是否真的找到了 qtbase_zh_CN.qm。
/// 没装 Qt 的翻译包（例如某些 CI 镜像）时为 false，此时标准控件仍是英文。
bool qtTranslationsLoaded();

} // namespace csm
