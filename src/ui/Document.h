#pragma once

// ---------------------------------------------------------------------------
//  一个打开的电路文档
//
//  多标签页里每个标签对应一个 Document：自己的画布与视图、文件路径、
//  未保存标记、撤销历史、分析参数、上一次的仿真结果。
//
//  MainWindow 只保留「当前文档」相关的指针（m_scene / m_view）作为缓存，
//  切标签时重新绑定，这样界面上那一大堆操作不用关心标签的存在。
// ---------------------------------------------------------------------------

#include "sim/Simulator.h"

#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <QVector>

namespace csm {

class SchematicScene;
class SchematicView;

struct Document {
    SchematicScene* scene = nullptr;
    SchematicView* view = nullptr;

    QString name = QStringLiteral("未命名");
    QString filePath;
    bool dirty = false;

    // 撤销 / 重做：整份电路的 JSON 快照 + 内容签名（用来判重）
    QVector<QJsonObject> history;
    QVector<QByteArray> historySignatures;
    int historyIndex = -1;
    bool restoring = false; // 正在恢复快照，别把它记进历史

    SimulationOptions options;      // 分析参数，随文件存盘
    SimulationResult lastResult;    // 最近一次仿真的结果，供「静态波形」使用
    bool hasResult = false;
    bool resultStale = false;       // 电路改过，上一次的结果已经不作数

    /// 标签页上显示的文字
    QString tabTitle() const { return name + (dirty ? QStringLiteral(" *") : QString()); }
};

} // namespace csm
