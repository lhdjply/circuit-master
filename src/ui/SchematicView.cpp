#include "ui/SchematicView.h"
#include "core/Language.h"

#include "ui/ComponentItem.h"
#include "ui/SchematicScene.h"
#include "ui/Symbols.h"

#include <QGraphicsPathItem>
#include <QContextMenuEvent>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QKeyEvent>
#include <QMenu>
#include <QLineF>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace csm {

namespace {

/// 两点是否已经在同一条水平 / 竖直线上（网格坐标，容差取半格）
bool axisAligned(const QPointF& a, const QPointF& b)
{
    return std::abs(a.x() - b.x()) < 0.5 || std::abs(a.y() - b.y()) < 0.5;
}

/// 在 a→b 之间补的那个拐点：先横后竖取 (b.x, a.y)，先竖后横取 (a.x, b.y)。
/// 两个坐标分别来自两个端点，所以拐点仍然落在网格上（或落在引脚上）。
QPointF wireElbow(const QPointF& a, const QPointF& b, bool horizontalFirst)
{
    return horizontalFirst ? QPointF(b.x(), a.y()) : QPointF(a.x(), b.y());
}

/// 这一段该先横还是先竖。规则是「**保持上一次的选择**」，只在光标和落点
/// 对齐（某条腿长度归零）时才重新认方向：
///
///   光标在落点正下方 / 正上方 → 只剩竖线 → 先竖（拐角在左下 / 左上）
///   光标在落点正右方 / 正左方 → 只剩横线 → 先横（拐角在右上 / 右下）
///   两条腿都在            → 保持上一次的选择，第一次才按主方向定
///
/// 这样「往下滑过去再往右拐」拐角留在左下；「往右滑过去再往下拐」留在右上；
/// 中途往左退到只剩一条竖线再往右，又会从竖线重新开始 —— 都不会跟着最后一段乱翻。
int wireElbowChoice(const QPointF& from, const QPointF& to, int current)
{
    const double dx = std::abs(to.x() - from.x());
    const double dy = std::abs(to.y() - from.y());
    if (dx < 0.5)
        return 2;   // 只剩竖线
    if (dy < 0.5)
        return 1;   // 只剩横线
    if (current != 0)
        return current;
    return dx >= dy ? 1 : 2;
}

} // namespace

SchematicView::SchematicView(SchematicScene* scene, QWidget* parent)
    : QGraphicsView(scene, parent)
    , m_scene(scene)
{
    setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing
                   | QPainter::SmoothPixmapTransform);
    setDragMode(QGraphicsView::RubberBandDrag);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setResizeAnchor(QGraphicsView::AnchorViewCenter);
    setViewportUpdateMode(QGraphicsView::FullViewportUpdate);
    setFrameShape(QFrame::NoFrame);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setAcceptDrops(true);
    viewport()->setAcceptDrops(true);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setBackgroundBrush(Qt::NoBrush);
    centerOn(300.0, 300.0);
}

void SchematicView::setTool(Tool tool)
{
    if (m_tool == tool)
        return;
    if (m_tool == Tool::Wire && tool != Tool::Wire)
        cancelWire();

    m_tool = tool;
    setDragMode(tool == Tool::Select ? QGraphicsView::RubberBandDrag : QGraphicsView::NoDrag);
    updateCursorAndHint();
    emit toolChanged();
}

void SchematicView::setKeyHints(const QString& rotate, const QString& run)
{
    // 没设置过时退回默认键名，避免提示里出现空白
    m_rotateKey = rotate.isEmpty() ? CSM_TR("Space") : rotate;
    m_runKey = run.isEmpty() ? QStringLiteral("F5") : run;
    updateCursorAndHint();
}

void SchematicView::updateCursorAndHint()
{
    switch (m_tool) {
    case Tool::Select:
        viewport()->setCursor(Qt::ArrowCursor);
        emit hintChanged(CSM_TR("Select mode: drag to move, double-click to edit, %1 to rotate")
                             .arg(m_rotateKey));
        break;
    case Tool::Wire:
        viewport()->setCursor(Qt::CrossCursor);
        emit hintChanged(CSM_TR("Wire mode: click to add a corner, double-click or right-click to finish, Esc to cancel"));
        break;
    }
}

void SchematicView::mousePressEvent(QMouseEvent* event)
{
    m_scene->setCursorScenePos(mapToScene(event->pos()));
    // 中键、右键都能拖着走图纸
    if (event->button() == Qt::MiddleButton || event->button() == Qt::RightButton) {
        // 布线时右键不是平移，按「画到哪一步」分三种：
        //   已经落了至少两个点（有线段）→ 保留这条线并断开，留在布线状态，
        //                                  下一条线跟它不相连
        //   只落了起点（还没有线段）    → 把起点清掉，也留在布线状态
        //   一个点都没有                → 见 mouseReleaseEvent：回到选择工具
        if (event->button() == Qt::RightButton && m_tool == Tool::Wire
            && !m_wirePoints.isEmpty()) {
            if (m_wirePoints.size() >= 2)
                finishWire();
            else
                cancelWire();
            event->accept();
            return;
        }
        m_panning = true;
        m_panButton = event->button();
        m_panStart = event->pos();
        if (event->button() == Qt::RightButton) {
            m_rightPressPos = event->pos();
            m_rightMoved = false;
            m_menuPending = false;
        }
        viewport()->setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }

    // 仿真运行中：左键直接点开关就是拨动它。这里先拦下来，不再进入拖动 ——
    // 仿真时去「拖」一个开关没有意义，反而会把画布弄乱。
    // 按钮开关是「按住才动作」，所以按下 / 松开分别处理。
    if (event->button() == Qt::LeftButton && m_tool == Tool::Select && m_liveRunning) {
        for (QGraphicsItem* g : items(event->pos())) {
            if (g->type() != ComponentItem::Type)
                continue;
            auto* item = static_cast<ComponentItem*>(g);
            const ComponentType type = item->component().type;
            if (type != ComponentType::Switch && type != ComponentType::PushButton)
                continue;
            m_scene->clearSelection();
            item->setSelected(true);
            if (type == ComponentType::PushButton) {
                m_pressedButton = item;
                emit pushButtonPressed(item);
            } else {
                emit switchToggled(item);
            }
            event->accept();
            return;
        }
    }

    if (event->button() == Qt::LeftButton && m_tool == Tool::Wire) {
        const QPointF pos = m_scene->snapToPin(mapToScene(event->pos()));
        if (m_wirePoints.isEmpty())
            startWire(pos);
        else
            addWirePoint(pos);
        updatePreview(pos);
        event->accept();
        return;
    }

    QGraphicsView::mousePressEvent(event);
}

void SchematicView::mouseMoveEvent(QMouseEvent* event)
{
    if (m_panning) {
        const QPoint delta = event->pos() - m_panStart;
        m_panStart = event->pos();
        // 超过 4 px 才算拖动，否则右键仍然是「点一下弹菜单」
        if (m_panButton == Qt::RightButton
            && (event->pos() - m_rightPressPos).manhattanLength() > 4)
            m_rightMoved = true;
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - delta.x());
        verticalScrollBar()->setValue(verticalScrollBar()->value() - delta.y());
        event->accept();
        return;
    }

    const QPointF scenePos = mapToScene(event->pos());
    m_scene->setCursorScenePos(scenePos);
    emit scenePositionChanged(scenePos);

    if (m_tool == Tool::Wire && !m_wirePoints.isEmpty()) {
        updatePreview(scenePos);
    }

    QGraphicsView::mouseMoveEvent(event);
}

void SchematicView::mouseReleaseEvent(QMouseEvent* event)
{
    // 按钮开关是瞬时的：不管松手时鼠标跑到哪儿，都把按住的这个还原
    if (m_pressedButton) {
        ComponentItem* released = m_pressedButton;
        m_pressedButton = nullptr;
        emit pushButtonReleased(released);
        event->accept();
        return;
    }
    if (m_panning
        && (event->button() == Qt::MiddleButton || event->button() == Qt::RightButton)) {
        const bool wasRight = m_panButton == Qt::RightButton;
        const bool dragged = m_rightMoved;
        const QPoint menuPos = m_menuPending ? m_pendingMenuPos : event->pos();
        m_panning = false;
        m_panButton = Qt::NoButton;
        m_rightMoved = false;
        m_menuPending = false;
        updateCursorAndHint();
        // 右键没拖动（或者干脆没收到 contextMenuEvent）：当成一次普通右键
        if (wasRight && !dragged) {
            if (m_tool == Tool::Wire) {
                // 布线状态下没在画线，右键点一下就是「不画了」——回到选择工具。
                // 拖动过的不算，那种情况是在平移图纸。
                setTool(Tool::Select);
            } else {
                showContextMenuAt(menuPos);
            }
        }
        event->accept();
        return;
    }

    QGraphicsView::mouseReleaseEvent(event);

    // 拖动或框选结束后记录一次历史（内容无变化时会被忽略）
    if (event->button() == Qt::LeftButton && m_tool == Tool::Select)
        m_scene->commitEdit();
}

void SchematicView::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && m_tool == Tool::Wire) {
        finishWire();
        event->accept();
        return;
    }
    // 仿真运行中双击开关：第一下按下已经拨动过，这里不再弹属性编辑
    if (event->button() == Qt::LeftButton && m_tool == Tool::Select && m_liveRunning) {
        for (QGraphicsItem* g : items(event->pos())) {
            if (g->type() == ComponentItem::Type
                && (static_cast<ComponentItem*>(g)->component().type == ComponentType::Switch
                    || static_cast<ComponentItem*>(g)->component().type
                        == ComponentType::PushButton)) {
                event->accept();
                return;
            }
        }
    }
    QGraphicsView::mouseDoubleClickEvent(event);
}

void SchematicView::wheelEvent(QWheelEvent* event)
{
    const double factor = event->angleDelta().y() > 0 ? 1.15 : 1.0 / 1.15;
    zoomBy(factor);
    event->accept();
}

void SchematicView::dragEnterEvent(QDragEnterEvent* event)
{
    if (event->mimeData()->hasFormat(QStringLiteral("application/x-csm-component"))) {
        event->acceptProposedAction();
        viewport()->setCursor(Qt::DragCopyCursor);
    } else {
        event->ignore();
    }
}

void SchematicView::dragMoveEvent(QDragMoveEvent* event)
{
    if (event->mimeData()->hasFormat(QStringLiteral("application/x-csm-component")))
        event->acceptProposedAction();
    else
        event->ignore();
}

void SchematicView::dropEvent(QDropEvent* event)
{
    if (!event->mimeData()->hasFormat(QStringLiteral("application/x-csm-component"))) {
        event->ignore();
        return;
    }

    bool ok = false;
    const QString payload =
        QString::fromUtf8(event->mimeData()->data(QStringLiteral("application/x-csm-component")));
    const QStringList parts = payload.split(QLatin1Char(':'));
    const int typeValue = parts.value(0).toInt(&ok);
    const int groundKind = parts.size() > 1 ? parts.at(1).toInt() : 0;
    const int gateKind = parts.size() > 2 ? parts.at(2).toInt() : 0;
    if (!ok) {
        event->ignore();
        return;
    }

    cancelWire();
    const QPointF scenePos = m_scene->snap(mapToScene(event->position().toPoint()));
    ComponentItem* item = m_scene->placeComponent(ComponentType(typeValue), scenePos);
    if (item) {
        if (item->component().type == ComponentType::Ground
            && groundKind != int(GroundKind::Gnd)) {
            Component comp = item->component();
            comp.groundKind = static_cast<GroundKind>(groundKind);
            item->setComponent(comp);
        }
        if (item->component().type == ComponentType::LogicGate) {
            Component comp = item->component();
            comp.gateKind = static_cast<GateKind>(gateKind);
            item->setComponent(comp);
        }
        m_scene->clearSelection();
        item->setSelected(true);
        setFocus();
        emit componentPlaced(item);
    }
    updateCursorAndHint();
    event->acceptProposedAction();
}

namespace {

/// 往菜单里挂「对齐与分布」子菜单。动作直接连到场景，不靠 exec 的返回值分发，
/// 所以放在右键菜单里也不用改后面的 if 链
void addArrangeMenu(QMenu* menu, SchematicScene* scene)
{
    if (!scene)
        return;
    const int count = scene->selectedComponents().size();
    QMenu* arrange = menu->addMenu(CSM_TR("Align and Distribute"));
    arrange->setEnabled(count >= 2);

    const QVector<QPair<QString, SchematicScene::AlignMode>> aligns = {
        {CSM_TR("Align left"), SchematicScene::AlignMode::Left},
        {CSM_TR("Center horizontally"), SchematicScene::AlignMode::CenterH},
        {CSM_TR("Right align"), SchematicScene::AlignMode::Right},
        {CSM_TR("Align top"), SchematicScene::AlignMode::Top},
        {CSM_TR("Center vertically"), SchematicScene::AlignMode::CenterV},
        {CSM_TR("Align bottom"), SchematicScene::AlignMode::Bottom},
    };
    for (const auto& entry : aligns) {
        QAction* action = arrange->addAction(entry.first);
        action->setEnabled(count >= 2);
        const SchematicScene::AlignMode mode = entry.second;
        QObject::connect(action, &QAction::triggered, scene,
                         [scene, mode] { scene->alignSelection(mode); });
    }
    arrange->addSeparator();
    const QVector<QPair<QString, Qt::Orientation>> distributes = {
        {CSM_TR("Distribute horizontally"), Qt::Horizontal},
        {CSM_TR("Distribute vertically"), Qt::Vertical},
    };
    for (const auto& entry : distributes) {
        QAction* action = arrange->addAction(entry.first);
        action->setEnabled(count >= 3);
        const Qt::Orientation orientation = entry.second;
        QObject::connect(action, &QAction::triggered, scene, [scene, orientation] {
            scene->distributeSelection(orientation);
        });
    }
}

} // namespace

void SchematicView::contextMenuEvent(QContextMenuEvent* event)
{
    // 右键按下就进入了平移：这里先记下位置，松手时若不是拖动再弹菜单。
    // 不同平台派发 contextMenuEvent 的时机不一样（按下 / 松开），统一在这里收口
    if (m_panning && m_panButton == Qt::RightButton) {
        m_pendingMenuPos = event->pos();
        m_menuPending = true;
        event->accept();
        return;
    }
    if (m_tool == Tool::Wire) {
        event->ignore();
        return;
    }
    showContextMenuAt(event->pos());
}

void SchematicView::showContextMenuAt(const QPoint& pos)
{
    ComponentItem* component = nullptr;
    const QList<QGraphicsItem*> hitItems = items(pos);
    for (QGraphicsItem* item : hitItems) {
        if (item->type() == ComponentItem::Type) {
            component = static_cast<ComponentItem*>(item);
            break;
        }
    }

    if (component && !component->isSelected()) {
        m_scene->clearSelection();
        component->setSelected(true);
    }

    QMenu menu(this);
    if (component) {
        QAction* rotate = menu.addAction(CSM_TR("Rotate 90°"));
        QAction* flipH = menu.addAction(CSM_TR("Flip horizontally"));
        QAction* flipV = menu.addAction(CSM_TR("Flip vertically"));
        QAction* resetLabels = menu.addAction(CSM_TR("Reset label positions"));
        addArrangeMenu(&menu, m_scene);
        menu.addSeparator();
        QAction* cut = menu.addAction(CSM_TR("Cut"));
        QAction* copy = menu.addAction(CSM_TR("Copy"));
        QAction* paste = menu.addAction(CSM_TR("Paste"));
        paste->setEnabled(m_scene->hasClipboard());
        menu.addSeparator();
        QAction* edit = menu.addAction(CSM_TR("Edit parameters…"));
        QAction* remove = menu.addAction(CSM_TR("Delete"));

        QAction* chosen = menu.exec(viewport()->mapToGlobal(pos));
        if (!chosen)
            return;
        if (chosen == rotate) {
            component->setRotationAngle(component->rotationAngle() - 90); // 逆时针
        } else if (chosen == flipH) {
            component->setMirrored(!component->mirroredX(), component->mirroredY());
        } else if (chosen == flipV) {
            component->setMirrored(component->mirroredX(), !component->mirroredY());
        } else if (chosen == resetLabels) {
            component->resetLabelOffsets();
        } else if (chosen == cut) {
            m_scene->cutSelection();
        } else if (chosen == copy) {
            m_scene->copySelection();
        } else if (chosen == paste) {
            m_scene->pasteClipboard();
        } else if (chosen == edit) {
            emit componentEditRequested(component);
        } else if (chosen == remove) {
            m_scene->deleteSelection();
        }
    } else {
        const QPointF scenePos = m_scene->snap(mapToScene(pos));
        QAction* junction = menu.addAction(CSM_TR("Add junction here"));
        junction->setEnabled(m_scene->hasCrossingAt(scenePos));
        QAction* paste = menu.addAction(CSM_TR("Paste"));
        paste->setEnabled(m_scene->hasClipboard());
        menu.addSeparator();
        QAction* pageSetup = menu.addAction(CSM_TR("Page Setup…"));
        QAction* chosen = menu.exec(viewport()->mapToGlobal(pos));
        if (chosen == junction)
            m_scene->connectWiresAt(scenePos);
        else if (chosen == paste)
            m_scene->pasteClipboard();
        else if (chosen == pageSetup)
            emit pageSetupRequested();
    }

    m_scene->refresh();
}

void SchematicView::keyPressEvent(QKeyEvent* event)
{
    if (event->matches(QKeySequence::Cut)) {
        m_scene->cutSelection();
        event->accept();
        return;
    }
    if (event->matches(QKeySequence::Copy)) {
        m_scene->copySelection();
        event->accept();
        return;
    }
    if (event->matches(QKeySequence::Paste)) {
        m_scene->pasteClipboard();
        event->accept();
        return;
    }

    // Esc 由 MainWindow 的「取消」动作处理（可在设置里改键），这里不再拦
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        if (m_tool == Tool::Wire && !m_wirePoints.isEmpty()) {
            finishWire();
            event->accept();
            return;
        }
    }
    QGraphicsView::keyPressEvent(event);
}

void SchematicView::startWire(const QPointF& scenePos)
{
    m_wirePoints.clear();
    m_wirePoints.append(scenePos);

    // 两件东西：已经落下的段用实线，还没定的橡皮筋用虚线
    if (!m_wirePlaced) {
        m_wirePlaced = new QGraphicsPathItem();
        QPen pen(Theme::wire(), 2.0, Qt::SolidLine);
        pen.setCapStyle(Qt::RoundCap);
        pen.setJoinStyle(Qt::MiterJoin);
        m_wirePlaced->setPen(pen);
        m_wirePlaced->setZValue(5.0);
        m_scene->addItem(m_wirePlaced);
    }
    if (!m_preview) {
        m_preview = new QGraphicsPathItem();
        QPen pen(Theme::wire(), 2.0, Qt::DashLine);
        pen.setCapStyle(Qt::RoundCap);
        m_preview->setPen(pen);
        m_preview->setZValue(5.0);
        m_scene->addItem(m_preview);
    }
}

void SchematicView::addWirePoint(const QPointF& scenePos)
{
    if (m_wirePoints.isEmpty()) {
        startWire(scenePos);
        return;
    }
    const QPointF last = m_wirePoints.last();
    if (QLineF(last, scenePos).length() < 1.0)
        return;

    // 走线只走直角：斜着拖出来的那一段拆成两段，中间插一个拐点。
    // 存进来的折线本身就已经是正交的，预览与落笔用的是同一份点，不会「看到的
    // 是斜线、画出来是直角」。
    const int choice = wireElbowChoice(last, scenePos, m_wireElbow);
    if (!axisAligned(last, scenePos))
        m_wirePoints.append(wireElbow(last, scenePos, choice == 1));
    m_wirePoints.append(scenePos);
    m_wireElbow = 0;   // 下一段重新认方向
}

void SchematicView::updatePreview(const QPointF& scenePos)
{
    if (!m_preview || m_wirePoints.isEmpty())
        return;

    // 已经落下的部分：实线
    QPainterPath placed;
    placed.moveTo(m_wirePoints.first());
    for (int i = 1; i < m_wirePoints.size(); ++i)
        placed.lineTo(m_wirePoints.at(i));
    if (m_wirePlaced)
        m_wirePlaced->setPath(placed);

    // 还没定的那一段：虚线（同样只走直角，和落笔时补的拐点保持一致）
    const QPointF target = m_scene->snapToPin(scenePos);
    const QPointF last = m_wirePoints.last();
    m_wireElbow = wireElbowChoice(last, target, m_wireElbow);

    QPainterPath rubber;
    rubber.moveTo(last);
    if (!axisAligned(last, target))
        rubber.lineTo(wireElbow(last, target, m_wireElbow == 1));
    rubber.lineTo(target);
    m_preview->setPath(rubber);
}

void SchematicView::finishWire()
{
    if (m_wirePoints.size() >= 2) {
        WireItem* drawn = m_scene->addWire(m_wirePoints);
        // 画出来的线和已有导线首尾相接且共线时并成一条（仅在绘制时发生）
        m_scene->mergeWireWithNeighbors(drawn);
        m_scene->commitEdit(); // 画完一段线记一次历史
    }

    m_wirePoints.clear();
    clearWirePreview();
}

void SchematicView::clearWirePreview()
{
    if (m_wirePlaced) {
        m_scene->removeItem(m_wirePlaced);
        delete m_wirePlaced;
        m_wirePlaced = nullptr;
    }
    if (m_preview) {
        m_scene->removeItem(m_preview);
        delete m_preview;
        m_preview = nullptr;
    }
}

void SchematicView::cancelWire()
{
    m_wirePoints.clear();
    clearWirePreview();
}

void SchematicView::zoomBy(double factor)
{
    const double current = transform().m11();
    const double target = current * factor;
    if (target < 0.15 || target > 4.0)
        return;
    scale(factor, factor);
    emit scaleChanged(transform().m11());
}

void SchematicView::zoomFit()
{
    const QRectF bounds = m_scene->itemsBoundingRect();
    if (bounds.isEmpty()) {
        resetTransform();
        centerOn(300.0, 300.0);
        return;
    }
    fitInView(bounds.adjusted(-60.0, -60.0, 60.0, 60.0), Qt::KeepAspectRatio);
    emit scaleChanged(transform().m11());
}

void SchematicView::zoomFitPage()
{
    const QRectF page = m_scene->pageSetup().sceneRect();
    if (!page.isValid() || page.isEmpty()) {
        zoomFit();
        return;
    }
    // 留一点边：纸框贴着视图边缘时，边框线会被裁掉一半
    fitInView(page.adjusted(-24.0, -24.0, 24.0, 24.0), Qt::KeepAspectRatio);
    emit scaleChanged(transform().m11());
}

void SchematicView::rotateSelection()
{
    const QList<QGraphicsItem*> selected = m_scene->selectedItems();
    for (QGraphicsItem* item : selected) {
        if (item->type() != ComponentItem::Type)
            continue;
        auto* comp = static_cast<ComponentItem*>(item);
        comp->setRotationAngle(comp->rotationAngle() - 90); // 逆时针
    }
    m_scene->refresh();
}

void SchematicView::deleteSelection()
{
    m_scene->deleteSelection();
}

} // namespace csm
