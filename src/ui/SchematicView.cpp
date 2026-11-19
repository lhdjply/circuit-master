#include "ui/SchematicView.h"

#include "ui/ComponentItem.h"
#include "ui/SchematicScene.h"
#include "ui/Symbols.h"

#include <QGraphicsPathItem>
#include <QApplication>
#include <QContextMenuEvent>
#include <QTimer>
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

SchematicView::SchematicView(SchematicScene* scene, QWidget* parent)
    : QGraphicsView(scene, parent)
    , m_scene(scene)
{
#if defined(Q_OS_ANDROID)
    // 要收 QTouchEvent 才能做双指缩放；单指仍交给 Qt 合成鼠标事件
    viewport()->setAttribute(Qt::WA_AcceptTouchEvents, true);
    viewport()->setAttribute(Qt::WA_Hover, false);
#endif
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

void SchematicView::updateCursorAndHint()
{
    switch (m_tool) {
    case Tool::Select:
        viewport()->setCursor(Qt::ArrowCursor);
        emit hintChanged(QStringLiteral("选择模式：拖动元件移动，双击元件编辑参数，空格键旋转"));
        break;
    case Tool::Wire:
        viewport()->setCursor(Qt::CrossCursor);
        emit hintChanged(QStringLiteral("连线模式：单击放置拐点，双击或右键结束，Esc 取消"));
        break;
    }
}

void SchematicView::mousePressEvent(QMouseEvent* event)
{
    m_scene->setCursorScenePos(mapToScene(event->pos()));
    if (event->button() == Qt::MiddleButton) {
        m_panning = true;
        m_panStart = event->pos();
        viewport()->setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }

    if (event->button() == Qt::RightButton && m_tool == Tool::Wire && !m_wirePoints.isEmpty()) {
        finishWire();
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

#if defined(Q_OS_ANDROID)
    // 手指按下：开始计长按。真鼠标（带触控板/鼠标的平板）不参与
    if (event->button() == Qt::LeftButton && m_tool == Tool::Select
        && event->source() == Qt::MouseEventSynthesizedByQt) {
        startLongPress(event->pos());
    }
#endif

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
#if defined(Q_OS_ANDROID)
    // 手指划开了就是拖拽，不是长按
    if (m_longPressTimer && m_longPressTimer->isActive()
        // 指腹按下去本来就会抖几像素，阈值给到 24（≈9dp）；拖拽很快就会超过它
        && (event->pos() - m_longPressPos).manhattanLength() > 24) {
        m_longPressTimer->stop();
    }
#endif
    if (m_panning) {
        const QPoint delta = event->pos() - m_panStart;
        m_panStart = event->pos();
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - delta.x());
        verticalScrollBar()->setValue(verticalScrollBar()->value() - delta.y());
        event->accept();
        return;
    }

    const QPointF scenePos = mapToScene(event->pos());
    m_scene->setCursorScenePos(scenePos);
    emit scenePositionChanged(scenePos);

    if (m_tool == Tool::Wire && !m_wirePoints.isEmpty())
        updatePreview(scenePos);

    QGraphicsView::mouseMoveEvent(event);
}

void SchematicView::mouseReleaseEvent(QMouseEvent* event)
{
#if defined(Q_OS_ANDROID)
    if (m_longPressTimer)
        m_longPressTimer->stop();
    if (m_longPressFired) {
        // 长按已经处理过，别再当成一次点击（否则会开始拖动 / 改选中）
        m_longPressFired = false;
        event->accept();
        return;
    }
#endif
    // 按钮开关是瞬时的：不管松手时鼠标跑到哪儿，都把按住的这个还原
    if (m_pressedButton) {
        ComponentItem* released = m_pressedButton;
        m_pressedButton = nullptr;
        emit pushButtonReleased(released);
        event->accept();
        return;
    }
    if (event->button() == Qt::MiddleButton && m_panning) {
        m_panning = false;
        updateCursorAndHint();
        event->accept();
        return;
    }

    QGraphicsView::mouseReleaseEvent(event);

    // 拖动或框选结束后记录一次历史（内容无变化时会被忽略）
    if (event->button() == Qt::LeftButton && m_tool == Tool::Select)
        m_scene->commitEdit();
}

#if defined(Q_OS_ANDROID)

bool SchematicView::viewportEvent(QEvent* event)
{
    // 手机上唯一能缩放的手势。单指触摸要放过去 —— 让 Qt 合成鼠标事件，
    // 否则拖动元件、画线全都会失灵。
    switch (event->type()) {
    case QEvent::TouchBegin:
    case QEvent::TouchUpdate:
    case QEvent::TouchEnd: {
        auto* touch = static_cast<QTouchEvent*>(event);
        const QList<QTouchEvent::TouchPoint>& points = touch->points();

        int active = 0;
        QPointF center;
        for (const QTouchEvent::TouchPoint& point : points) {
            if (point.state() == QEventPoint::State::Released)
                continue;
            ++active;
            center += point.position();
        }

        if (active < 2) {
            // 单指：交回给基类，继续按鼠标处理（拖动、框选、连线都靠它）
            m_pinching = false;
            return QGraphicsView::viewportEvent(event);
        }

        if (active == 2) {
            QPointF a;
            QPointF b;
            bool first = true;
            for (const QTouchEvent::TouchPoint& point : points) {
                if (point.state() == QEventPoint::State::Released)
                    continue;
                if (first) { a = point.position(); first = false; }
                else       { b = point.position(); }
            }
            const double distance = QLineF(a, b).length();
            const QPointF pinchCenter = (a + b) / 2.0;

            if (!m_pinching) {
                m_pinching = true;
                m_pinchDistance = distance;
                m_pinchCenter = pinchCenter;
                return true;
            }

            if (m_pinchDistance > 1.0 && distance > 1.0) {
                zoomBy(distance / m_pinchDistance);
                // 两指整体移动当平移用
                const QPointF delta = pinchCenter - m_pinchCenter;
                horizontalScrollBar()->setValue(horizontalScrollBar()->value()
                                                - int(delta.x()));
                verticalScrollBar()->setValue(verticalScrollBar()->value()
                                              - int(delta.y()));
            }
            m_pinchDistance = distance;
            m_pinchCenter = pinchCenter;
            if (event->type() == QEvent::TouchEnd)
                m_pinching = false;
            return true;
        }
        break;
    }
    default:
        break;
    }
    return QGraphicsView::viewportEvent(event);
}

#endif // Q_OS_ANDROID

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
    QMenu* arrange = menu->addMenu(QObject::tr("对齐与分布"));
    arrange->setEnabled(count >= 2);

    const QVector<QPair<QString, SchematicScene::AlignMode>> aligns = {
        {QObject::tr("左对齐"), SchematicScene::AlignMode::Left},
        {QObject::tr("水平居中"), SchematicScene::AlignMode::CenterH},
        {QObject::tr("右对齐"), SchematicScene::AlignMode::Right},
        {QObject::tr("顶对齐"), SchematicScene::AlignMode::Top},
        {QObject::tr("垂直居中"), SchematicScene::AlignMode::CenterV},
        {QObject::tr("底对齐"), SchematicScene::AlignMode::Bottom},
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
        {QObject::tr("水平等距"), Qt::Horizontal},
        {QObject::tr("垂直等距"), Qt::Vertical},
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

#if defined(Q_OS_ANDROID)

// 长按弹右键菜单：手机上没有右键，旋转 / 翻转 / 剪贴板 / 编辑参数这些
// 全在右键菜单里，不给入口就一个都用不了。
// 用鼠标事件的 source() 区分「手指合成」与真鼠标 —— 接了触摸事件之后，
// 单指仍由 Qt 合成鼠标事件，所以这里还能收到。
void SchematicView::startLongPress(const QPoint& pos)
{
    m_longPressPos = pos;
    m_longPressFired = false;
    if (!m_longPressTimer) {
        m_longPressTimer = new QTimer(this);
        m_longPressTimer->setSingleShot(true);
        m_longPressTimer->setInterval(500); // 500ms 是 Android 的惯例
        connect(m_longPressTimer, &QTimer::timeout, this, [this] {
            m_longPressFired = true;
            QContextMenuEvent menuEvent(QContextMenuEvent::Mouse, m_longPressPos,
                                        viewport()->mapToGlobal(m_longPressPos));
            QApplication::sendEvent(this, &menuEvent);
        });
    }
    m_longPressTimer->start();
}

#endif // Q_OS_ANDROID

void SchematicView::contextMenuEvent(QContextMenuEvent* event)
{
    if (m_tool == Tool::Wire) {
        event->ignore();
        return;
    }

    ComponentItem* component = nullptr;
    const QList<QGraphicsItem*> hitItems = items(event->pos());
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
        QAction* rotate = menu.addAction(QStringLiteral("旋转 90°"));
        QAction* flipH = menu.addAction(QStringLiteral("水平翻转"));
        QAction* flipV = menu.addAction(QStringLiteral("垂直翻转"));
        QAction* resetLabels = menu.addAction(QStringLiteral("还原标签位置"));
        addArrangeMenu(&menu, m_scene);
        menu.addSeparator();
        QAction* cut = menu.addAction(QStringLiteral("剪切"));
        QAction* copy = menu.addAction(QStringLiteral("复制"));
        QAction* paste = menu.addAction(QStringLiteral("粘贴"));
        paste->setEnabled(m_scene->hasClipboard());
        menu.addSeparator();
        QAction* edit = menu.addAction(QStringLiteral("编辑参数…"));
        QAction* remove = menu.addAction(QStringLiteral("删除"));

        QAction* chosen = menu.exec(event->globalPos());
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
        const QPointF scenePos = m_scene->snap(mapToScene(event->pos()));
        QAction* junction = menu.addAction(QStringLiteral("在此处打交点"));
        junction->setEnabled(m_scene->hasCrossingAt(scenePos));
        QAction* paste = menu.addAction(QStringLiteral("粘贴"));
        paste->setEnabled(m_scene->hasClipboard());
        QAction* chosen = menu.exec(event->globalPos());
        if (chosen == junction)
            m_scene->connectWiresAt(scenePos);
        else if (chosen == paste)
            m_scene->pasteClipboard();
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

    if (event->key() == Qt::Key_Escape) {
        if (m_tool == Tool::Wire) {
            cancelWire();
            setTool(Tool::Select);
        } else {
            m_scene->clearSelection();
        }
        event->accept();
        return;
    }
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

    if (!m_preview) {
        m_preview = new QGraphicsPathItem();
        QPen pen(theme::wire, 2.0, Qt::DashLine);
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
    if (QLineF(m_wirePoints.last(), scenePos).length() < 1.0)
        return;
    m_wirePoints.append(scenePos);
}

void SchematicView::updatePreview(const QPointF& scenePos)
{
    if (!m_preview || m_wirePoints.isEmpty())
        return;

    QPainterPath path;
    path.moveTo(m_wirePoints.first());
    for (int i = 1; i < m_wirePoints.size(); ++i)
        path.lineTo(m_wirePoints.at(i));
    path.lineTo(m_scene->snapToPin(scenePos));
    m_preview->setPath(path);
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
    if (m_preview) {
        m_scene->removeItem(m_preview);
        delete m_preview;
        m_preview = nullptr;
    }
}

void SchematicView::cancelWire()
{
    m_wirePoints.clear();
    if (m_preview) {
        m_scene->removeItem(m_preview);
        delete m_preview;
        m_preview = nullptr;
    }
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
