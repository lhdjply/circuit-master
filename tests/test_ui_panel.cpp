#include "core/Circuit.h"
#include "core/Netlist.h"
#include "csm_platform.h"
#include "csm_version.h"
#include "ui/ComponentItem.h"
#include "ui/I18n.h"
#include "ui/MainWindow.h"
#include "ui/MultimeterWindow.h"
#include "ui/PropertyPanel.h"
#include "ui/SchematicScene.h"
#include "ui/SchematicView.h"
#include "ui/Symbols.h"
#include "ui/UpdateDialog.h"
#include "ui/WaveformWindow.h"
#include "ui/WireItem.h"

#include <QAbstractAxis>
#include <QAction>
#include <QApplication>
#include <QChart>
#include <QChartView>
#include <QCheckBox>
#include <QDataStream>
#include <QImage>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QGraphicsSceneMouseEvent>
#include <QIcon>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QLineSeries>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScopeGuard>
#include <QSet>
#include <QSignalSpy>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QValueAxis>
#include <QWidget>
#include <cmath>
#include <cstdio>

using namespace csm;

class UiPanelTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void propertyPanelPlaceholderIsCentred();
    void valueWithUnits();
    void rotationCanBeSetFromPanel();
    void transformerHasFourPinsAndPanel();
    void transformerWindingKindSwitchable();
    void groundCanBeRotated();
    void capacitorPolarityToggle();
    void optocouplerSymbolAndPanel();
    void transistorSymbolsRender();
    void editPropertiesAndRotate();
    void libraryFiltersByCategory();
    void logicGateRowsInPanel();
    void logicGateIconsDiffer();
    void libraryDragPixmapHasNoSelectionBackground();

private:

    /// 把窗口截图存到 build/shots/ 下（目录不存在就建出来）。
    /// 拆成独立可执行文件后，不能再指望「别的用例先建好了目录」——
    /// 干净构建时 shots/ 并不存在，grab().save() 会直接失败。
    static bool saveShot(QWidget* widget, const QString& fileName)
    {
        QDir dir(QCoreApplication::applicationDirPath());
        if (!dir.mkpath(QStringLiteral("shots")))
            return false;
        return widget->grab().save(dir.filePath(QStringLiteral("shots/") + fileName));
    }

    // 下面几个是本文件自用的界面操作辅助。为了每个测试文件彻底独立
    // （删掉别的文件也不影响自己），各留一份，不再共用头文件。
    static void settle(int rounds = 3)
    {
        for (int i = 0; i < rounds; ++i)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }

    static void waitShown(MainWindow& window)
    {
        QTRY_VERIFY_WITH_TIMEOUT(window.isVisible(), 3000);
        settle();
    }
};

void UiPanelTest::initTestCase()
{
    // 和 main.cpp 一样装上 Qt 自带的中文翻译（输入框右键菜单等）
    // 崩溃时缓冲区里的输出会丢：Windows 上 stdout 重定向到管道是全缓冲的，
    // CI 上只会看到「Failed」却不知道停在哪一步。改成不缓冲，一行一行往外写。
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::setvbuf(stderr, nullptr, _IONBF, 0);
    csm::installQtTranslations(qApp);
}

void UiPanelTest::propertyPanelPlaceholderIsCentred()
{
    // 刚启动时属性面板只有一句提示。以前这句提示被塞进 QStackedWidget 里，
    // 而 stack 的 sizeHint 取所有页里最大的 —— 空态被表单页一起撑到上千像素高，
    // 提示文字被顶到很下面，右边还多出一条滚动条。
    MainWindow window;
    window.resize(1400, 900);
    window.show();
    waitShown(window);

    auto* props = window.findChild<PropertyPanel*>();
    QVERIFY(props);

    QLabel* placeholder = nullptr;
    for (QLabel* label : props->findChildren<QLabel*>()) {
        if (label->text().contains(QStringLiteral("在原理图中选择")))
            placeholder = label;
    }
    QVERIFY2(placeholder, "找不到空态提示");
    QVERIFY2(placeholder->isVisible(), "刚启动时应当显示空态提示");

    // 提示区域应当占满面板，且文字在中线上
    const int centre = placeholder->y() + placeholder->height() / 2;
    const int panelCentre = props->height() / 2;
    QVERIFY2(std::abs(centre - panelCentre) <= 2,
             qPrintable(QStringLiteral("提示中心 y=%1，面板中心 y=%2")
                            .arg(centre)
                            .arg(panelCentre)));

    // 面板不该因为空态被撑高（表单页那一堆字段不该参与空态的 sizeHint）
    QVERIFY2(props->height() <= 900,
             qPrintable(QStringLiteral("空态面板高 %1，被撑高了").arg(props->height())));

    // 选中元件后提示隐藏、表单出现
    auto* scene = window.findChild<SchematicScene*>();
    QVERIFY(scene);
    auto* item = scene->placeComponent(ComponentType::Resistor, QPointF(300, 300));
    item->setSelected(true);
    settle();
    QVERIFY2(!placeholder->isVisible(), "选中元件后提示应当隐藏");
    QVERIFY2(props->findChild<QStackedWidget*>()->isVisible(), "选中元件后表单应当显示");
}

void UiPanelTest::valueWithUnits()
{
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    waitShown(window);

    QAction* rcExample = nullptr;
    for (QAction* action : window.findChildren<QAction*>()) {
        if (action->text().contains(QStringLiteral("RC 充电")))
            rcExample = action;
    }
    QVERIFY(rcExample);
    rcExample->trigger();
    settle();

    auto* scene = window.findChild<SchematicScene*>();
    QVERIFY(scene);

    ComponentItem* resistor = nullptr;
    for (ComponentItem* item : scene->componentItems()) {
        if (item->component().refdes == QLatin1String("R1"))
            resistor = item;
    }
    QVERIFY(resistor);
    resistor->setSelected(true);
    settle();

    QLineEdit* valueEdit = nullptr;
    for (QLineEdit* edit : window.findChildren<QLineEdit*>()) {
        if (edit->placeholderText().contains(QStringLiteral("10kΩ")))
            valueEdit = edit;
    }
    QVERIFY(valueEdit);

    valueEdit->setText(QStringLiteral("2.2kΩ"));
    emit valueEdit->editingFinished();
    settle();
    QCOMPARE(resistor->component().value, 2200.0);

    // 单位不匹配：拒绝并标红
    valueEdit->setText(QStringLiteral("5V"));
    emit valueEdit->editingFinished();
    settle();
    QCOMPARE(resistor->component().value, 2200.0);
    QVERIFY(valueEdit->styleSheet().contains(QStringLiteral("dc2626")));

    // 不带单位按默认单位（Ω）
    valueEdit->setText(QStringLiteral("2.2k"));
    emit valueEdit->editingFinished();
    settle();
    QCOMPARE(resistor->component().value, 2200.0);
    QVERIFY(valueEdit->styleSheet().isEmpty());

    // 电容带单位
    ComponentItem* capacitor = nullptr;
    for (ComponentItem* item : scene->componentItems()) {
        if (item->component().refdes == QLatin1String("C1"))
            capacitor = item;
    }
    QVERIFY(capacitor);
    scene->clearSelection();
    capacitor->setSelected(true);
    settle();
    QVERIFY(valueEdit->text().contains(QStringLiteral("µF")));

    valueEdit->setText(QStringLiteral("0.0001F"));
    emit valueEdit->editingFinished();
    settle();
    QVERIFY(std::abs(capacitor->component().value - 100e-6) < 1e-15);

    // 重新选中后数值以工程记数法显示
    capacitor->setSelected(false);
    capacitor->setSelected(true);
    settle();
    QVERIFY(valueEdit->text().contains(QStringLiteral("100")));
}

void UiPanelTest::rotationCanBeSetFromPanel()
{
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    waitShown(window);

    auto* scene = window.findChild<SchematicScene*>();
    QVERIFY(scene);
    ComponentItem* item = scene->placeComponent(ComponentType::Resistor, QPointF(300, 300));
    QVERIFY(item);
    scene->clearSelection();
    item->setSelected(true);
    settle();

    // 属性面板里的「旋转角度」可以填任意角度
    QLineEdit* rotation = nullptr;
    for (QLineEdit* edit : window.findChildren<QLineEdit*>())
        if (edit->text() == QStringLiteral("0") && edit->toolTip().contains(QStringLiteral("角度")))
            rotation = edit;
    QVERIFY2(rotation, "属性面板应当有「旋转角度」输入框");

    const auto setAngle = [&](const QString& text) {
        rotation->setText(text);
        emit rotation->editingFinished();
        settle();
    };

    setAngle(QStringLiteral("90"));
    QCOMPARE(item->component().rotation, 90);
    QCOMPARE(item->pinScenePositions().at(0), QPointF(300, 270));

    setAngle(QStringLiteral("180"));
    QCOMPARE(item->component().rotation, 180);
    QCOMPARE(item->pinScenePositions().at(0), QPointF(330, 300));

    // 自由角度：45° 不再是 90° 的整数倍
    setAngle(QStringLiteral("45"));
    QCOMPARE(item->component().rotation, 45);
    // 引脚吸附到 10 px 网格，斜放也能和导线端点对齐
    const QPointF pin45 = item->pinScenePositions().at(0);
    QCOMPARE(pin45, QPointF(280, 280));

    setAngle(QStringLiteral("0"));
    QCOMPARE(item->component().rotation, 0);
}

void UiPanelTest::transformerHasFourPinsAndPanel()
{
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    waitShown(window);

    auto* scene = window.findChild<SchematicScene*>();
    QVERIFY(scene);
    ComponentItem* t = scene->placeComponent(ComponentType::Transformer, QPointF(300, 300));
    QVERIFY(t);
    QCOMPARE(t->pinScenePositions().size(), 4);
    QCOMPARE(t->component().refdes.left(1), QStringLiteral("T"));
    // 默认 1:2、初级 1 H
    QCOMPARE(t->component().value, 2.0);
    QCOMPARE(t->component().lpri, 1.0);

    scene->clearSelection();
    t->setSelected(true);
    settle();

    // 属性面板里应当有「初级电感 L1」
    bool found = false;
    for (QLabel* label : window.findChildren<QLabel*>())
        if (label->isVisible() && label->text().contains(QStringLiteral("初级电感")))
            found = true;
    QVERIFY2(found, "属性面板应当显示初级电感");
}

void UiPanelTest::transformerWindingKindSwitchable()
{
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    waitShown(window);

    auto* scene = window.findChild<SchematicScene*>();
    QVERIFY(scene);
    ComponentItem* t = scene->placeComponent(ComponentType::Transformer, QPointF(300, 300));
    QVERIFY(t);
    QCOMPARE(t->pinScenePositions().size(), 4);
    QCOMPARE(t->component().transformerKind, TransformerKind::Single);

    // 属性面板里把绕组形式换成「初级 + 辅助绕组 + 次级」→ 引脚变成 6 个
    QComboBox* windings = nullptr;
    for (QComboBox* combo : window.findChildren<QComboBox*>())
        if (combo->count() == 2
            && combo->itemText(1).contains(QStringLiteral("辅助绕组")))
            windings = combo;
    QVERIFY2(windings, "属性面板应当有「绕组形式」下拉框");

    scene->clearSelection();
    t->setSelected(true);
    settle();
    windings->setCurrentIndex(1);
    settle();

    QCOMPARE(t->component().transformerKind, TransformerKind::PrimaryAuxSecondary);
    QCOMPARE(t->pinScenePositions().size(), 6);

    // 匝数比一行写完 N1:N2:N3
    QLineEdit* ratioEdit = nullptr;
    for (QLineEdit* edit : window.findChildren<QLineEdit*>())
        if (edit->isVisible() && edit->text().contains(QLatin1Char(':')))
            ratioEdit = edit;
    QVERIFY2(ratioEdit, "应当有「匝数比 N1:N2:N3」输入框");
    QCOMPARE(ratioEdit->text(), QStringLiteral("1:1:2"));

    ratioEdit->setText(QStringLiteral("1:1:3"));
    emit ratioEdit->editingFinished();
    settle();
    QCOMPARE(t->component().value, 3.0);
    QCOMPARE(t->component().auxRatio, 1.0);

    ratioEdit->setText(QStringLiteral("2:1:4"));
    emit ratioEdit->editingFinished();
    settle();
    QCOMPARE(t->component().value, 2.0);    // 4/2
    QCOMPARE(t->component().auxRatio, 0.5); // 1/2
}

void UiPanelTest::groundCanBeRotated()
{
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    waitShown(window);

    auto* scene = window.findChild<SchematicScene*>();
    QVERIFY(scene);

    auto* source = scene->placeComponent(ComponentType::VoltageSource, QPointF(100, 300));
    source->setRotationAngle(90);
    auto* ground = scene->placeComponent(ComponentType::Ground, QPointF(100, 400));
    QVERIFY(ground);
    scene->addWire({QPointF(100, 330), QPointF(100, 400)});
    settle();

    scene->clearSelection();
    ground->setSelected(true);
    settle();

    QAction* rotate = window.findChild<QAction*>(QStringLiteral("actRotate"));
    QVERIFY(rotate);
    QVERIFY2(rotate->isEnabled(), "接地符号应当可以旋转");

    rotate->trigger();
    settle();
    QCOMPARE(ground->component().rotation, 270); // 逆时针 90°

    rotate->trigger();
    settle();
    QCOMPARE(ground->component().rotation, 180);

    // 文字跟着转，包围盒要能放下任意方向的符号与文字
    const QRectF box = ground->boundingRect();
    for (const QPointF& corner : {QPointF(-42, -34), QPointF(42, -34), QPointF(-42, 42),
                                  QPointF(42, 42)})
        QVERIFY2(box.contains(corner), "包围盒应当覆盖旋转后的符号与文字");

    // 转完仍然连着：网表有效、地节点还在
    const Netlist net = scene->toCircuit().buildNetlist();
    QVERIFY2(net.valid, qPrintable(net.error));
    QCOMPARE(net.nodeOf(ground->component().id, 0), 0);
}

void UiPanelTest::capacitorPolarityToggle()
{
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    waitShown(window);

    auto* scene = window.findChild<SchematicScene*>();
    QVERIFY(scene);
    ComponentItem* cap = scene->placeComponent(ComponentType::Capacitor, QPointF(200, 200));
    QVERIFY(cap);
    QCOMPARE(cap->component().polarized, false);
    cap->setSelected(true);
    settle();

    QCheckBox* polarized = nullptr;
    for (QCheckBox* box : window.findChildren<QCheckBox*>()) {
        if (box->text().contains(QStringLiteral("有极性")))
            polarized = box;
    }
    QVERIFY(polarized);
    QVERIFY2(polarized->isVisible(), "电容应当显示极性选项");
    QVERIFY(!polarized->isChecked());

    polarized->setChecked(true);
    settle();
    QVERIFY2(cap->component().polarized, "勾选后元件应当标记为有极性");

    // 电阻不显示这一项
    ComponentItem* res = scene->placeComponent(ComponentType::Resistor, QPointF(400, 200));
    scene->clearSelection(); // 先取消电容的选中，面板才会切到电阻
    res->setSelected(true);
    settle();
    QVERIFY2(!polarized->isVisible(), "电阻不应显示极性选项");
}

void UiPanelTest::optocouplerSymbolAndPanel()
{
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    waitShown(window);

    auto* scene = window.findChild<SchematicScene*>();
    QVERIFY(scene);
    ComponentItem* opto = scene->placeComponent(ComponentType::Optocoupler, QPointF(250, 300));
    QVERIFY(opto);
    QCOMPARE(opto->pinScenePositions().size(), 4);
    QCOMPARE(opto->component().refdes.left(2), QStringLiteral("OC"));
    // 默认参数：红外 LED 的 Vf 与 CTR
    QCOMPARE(opto->component().vf, 1.2);
    // 默认按 LTV-817S-TA1-C：CTR 档位 C 取典型 300%
    QCOMPARE(opto->component().ctr, 3.0);

    scene->clearSelection();
    opto->setSelected(true);
    settle();

    // 属性面板要有 Vf 与 CTR 两栏
    QLineEdit* ctrEdit = nullptr;
    for (QLineEdit* edit : window.findChildren<QLineEdit*>()) {
        if (edit->text() == QStringLiteral("3") && edit->isVisible())
            ctrEdit = edit;
    }
    QVERIFY2(ctrEdit, "应当显示 CTR 输入框");

    QVERIFY(saveShot(&window, QStringLiteral("optocoupler.png")));
}

void UiPanelTest::transistorSymbolsRender()
{
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    waitShown(window);

    auto* scene = window.findChild<SchematicScene*>();
    QVERIFY(scene);

    // 五种三端器件摆一排，确认符号能画出来、引脚数正确
    const QVector<ComponentType> types = {
        ComponentType::Npn,  ComponentType::Pnp,  ComponentType::Nmos,
        ComponentType::Pmos, ComponentType::Igbt, ComponentType::Led,
        ComponentType::Diode};
    for (int i = 0; i < types.size(); ++i) {
        // 两行摆放，避免被右侧属性面板挡住
        const QPointF pos(150 + (i % 3) * 120, i < 3 ? 250 : 400);
        ComponentItem* item = scene->placeComponent(types.at(i), pos);
        QVERIFY(item);
        QCOMPARE(item->pinScenePositions().size(), pinCount(types.at(i)));
        QCOMPARE(item->component().refdes.left(1), typeSymbol(types.at(i)));
    }
    settle();

    QVERIFY(saveShot(&window, QStringLiteral("transistors.png")));
}

void UiPanelTest::editPropertiesAndRotate()
{
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    waitShown(window);

    auto* scene = window.findChild<SchematicScene*>();
    QVERIFY(scene);

    QAction* rcExample = nullptr;
    for (QAction* action : window.findChildren<QAction*>()) {
        if (action->text().contains(QStringLiteral("RC 充电")))
            rcExample = action;
    }
    QVERIFY(rcExample);
    rcExample->trigger();
    settle();

    ComponentItem* resistor = nullptr;
    for (ComponentItem* item : scene->componentItems()) {
        if (item->component().refdes == QLatin1String("R1"))
            resistor = item;
    }
    QVERIFY(resistor);
    resistor->setSelected(true);
    settle();

    QLineEdit* valueEdit = nullptr;
    for (QLineEdit* edit : window.findChildren<QLineEdit*>()) {
        if (edit->placeholderText().contains(QStringLiteral("10kΩ")))
            valueEdit = edit;
    }
    QVERIFY(valueEdit);
    valueEdit->setText(QStringLiteral("2k"));
    emit valueEdit->editingFinished();
    settle();
    QCOMPARE(resistor->component().value, 2000.0);

    QAction* rotateAction = nullptr;
    for (QAction* action : window.findChildren<QAction*>()) {
        if (action->text().contains(QStringLiteral("旋转")))
            rotateAction = action;
    }
    QVERIFY(rotateAction);
    // 示例中的 R1 初始为水平放置，逆时针旋转一次后应为 270°
    QCOMPARE(resistor->component().rotation, 0);
    rotateAction->trigger();
    QCOMPARE(resistor->component().rotation, 270);
}

void UiPanelTest::logicGateIconsDiffer()
{
    // 元件库里五种门各占一条，图标必须按「具体是哪个门」画。
    // 之前只把 ComponentType 传给 componentIcon，五条全画成同一个与门
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    waitShown(window);

    QComboBox* category = nullptr;
    for (QComboBox* box : window.findChildren<QComboBox*>()) {
        if (box->count() > 0 && box->itemText(0) == QStringLiteral("全部"))
            category = box;
    }
    QVERIFY(category);
    const int logicIndex = category->findText(QStringLiteral("数字逻辑"));
    QVERIFY(logicIndex > 0);
    category->setCurrentIndex(logicIndex);
    settle();

    QListWidget* library = nullptr;
    for (QListWidget* list : window.findChildren<QListWidget*>()) {
        if (list->count() > 0 && list->count() <= 8)
            library = list;
    }
    QVERIFY(library);
    QCOMPARE(library->count(), 5);

    QSet<QByteArray> seen;
    for (int i = 0; i < library->count(); ++i) {
        const QImage image = library->item(i)->icon().pixmap(QSize(60, 42)).toImage();
        QByteArray bytes;
        QDataStream stream(&bytes, QIODevice::WriteOnly);
        stream << image;
        seen.insert(bytes);
    }
    QCOMPARE(seen.size(), 5); // 五个门的图标互不相同
}

void UiPanelTest::logicGateRowsInPanel()
{
    // 逻辑门在属性面板里只有一行可调的参数：输出高电平。
    // 门类型不放面板 —— 元件库里与门 / 或门 / 非门…各占一条，换门就重放一个
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    waitShown(window);

    SchematicScene* scene = window.findChild<SchematicScene*>();
    QVERIFY(scene);
    auto* item = scene->placeComponent(ComponentType::LogicGate, QPointF(300, 300));
    QVERIFY(item);
    QCOMPARE(item->component().gateKind, GateKind::And);

    item->setSelected(true);
    settle();

    QVERIFY2(window.findChild<QComboBox*>(QStringLiteral("gateKindCombo")) == nullptr,
             "面板里不该再有门类型下拉框（元件库已经按门分类了）");

    // 输出高电平：默认 3.3 V，改成 5
    QLineEdit* high = nullptr;
    for (QLineEdit* edit : window.findChildren<QLineEdit*>()) {
        if (edit->placeholderText().contains(QStringLiteral("3.3")))
            high = edit;
    }
    QVERIFY2(high, "属性面板里应当有高电平输入框");
    high->setText(QStringLiteral("5"));
    high->editingFinished();
    settle();
    QVERIFY2(std::abs(item->component().logicHigh - 5.0) < 1e-9,
             qPrintable(QString::number(item->component().logicHigh)));

    // 电阻不该显示这一行（先清掉门的选择，否则面板还在显示门）
    item->setSelected(false);
    auto* resistor = scene->placeComponent(ComponentType::Resistor, QPointF(600, 300));
    QVERIFY(resistor);
    resistor->setSelected(true);
    settle();
    QVERIFY2(!high->isVisible(), "非逻辑门不该显示高电平输入框");
}

void UiPanelTest::libraryFiltersByCategory()
{
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    waitShown(window);

    QListWidget* library = nullptr;
    for (QListWidget* list : window.findChildren<QListWidget*>()) {
        if (list->count() > 0 && list->item(0)->text().contains(QStringLiteral("电阻")))
            library = list;
    }
    QVERIFY(library);
    const int allCount = library->count();
    QVERIFY(allCount >= 16);

    QComboBox* category = nullptr;
    for (QComboBox* box : window.findChildren<QComboBox*>()) {
        if (box->count() > 0 && box->itemText(0) == QStringLiteral("全部"))
            category = box;
    }
    QVERIFY(category);
    QCOMPARE(category->count(), 8); // 「全部」+ 7 个分类

    // 每个分类都应当有内容，且加起来不超过全部
    QMap<QString, int> counts;
    for (int i = 1; i < category->count(); ++i) {
        category->setCurrentIndex(i);
        settle();
        counts.insert(category->currentText(), library->count());
        QVERIFY2(library->count() > 0,
                 qPrintable(QStringLiteral("分类为空: ") + category->currentText()));
    }

    // 分类按「器件性质」划分，同类元件数量不该悬殊
    QCOMPARE(counts.value(QStringLiteral("基础")), 4);        // R / C / L / 变压器
    QCOMPARE(counts.value(QStringLiteral("半导体")), 7);      // 二极管、LED、三极管、MOS、IGBT
    QCOMPARE(counts.value(QStringLiteral("模拟 IC")), 4);     // 运放 / TL431 / 光耦 / OB2362
    QCOMPARE(counts.value(QStringLiteral("数字逻辑")), 5);     // 与 / 或 / 非 / 异或 / 同或
    QCOMPARE(counts.value(QStringLiteral("电源")), 2);        // 电压源 / 电流源
    QCOMPARE(counts.value(QStringLiteral("开关与接地")), 3);  // 开关 / 接地 / 网络标签
    QCOMPARE(counts.value(QStringLiteral("测量")), 3);        // TP / IP / 万用表

    int sum = 0;
    for (int n : counts)
        sum += n;
    QCOMPARE(sum, allCount);

    // 切回「全部」应恢复完整列表
    category->setCurrentIndex(0);
    settle();
    QCOMPARE(library->count(), allCount);
}

void UiPanelTest::libraryDragPixmapHasNoSelectionBackground()
{
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    waitShown(window);

    QListWidget* library = nullptr;
    for (QListWidget* list : window.findChildren<QListWidget*>()) {
        if (list->count() > 0 && list->item(0)->text().contains(QStringLiteral("电阻")))
            library = list;
    }
    QVERIFY(library);
    library->setCurrentRow(0);
    library->item(0)->setSelected(true);

    // 拖影应当只包含图标：取该行的图标按 72x48 渲染，背景透明
    const QIcon icon = library->item(0)->icon();
    QPixmap pixmap(72, 48);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    icon.paint(&painter, QRect(QPoint(0, 0), QSize(72, 48)), Qt::AlignCenter);
    painter.end();

    const QImage image = pixmap.toImage();
    int opaque = 0;
    int bluish = 0;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const QColor c = image.pixelColor(x, y);
            if (c.alpha() > 0) {
                ++opaque;
                // 选中背景是接近 #2563eb 的实心蓝
                if (c.blue() > 180 && c.red() < 90 && c.green() < 130)
                    ++bluish;
            }
        }
    }
    QVERIFY2(opaque > 0, "拖影为空");
    QVERIFY2(bluish < opaque / 2, "拖影仍带有蓝色选中背景");
}

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    // QTest 的日志默认写 stdout，而 Windows CI 上**这个进程的 stdout 收不到**
    // （实测一行都没进 ctest 的日志，QTest 的 PASS/FAIL 和 qwarn 全丢）。
    // 所以让它写到文件，跑完原样倒到 stderr —— stderr 各平台都通。
    QStringList args;
    for (int i = 0; i < argc; ++i)
        args << QString::fromLocal8Bit(argv[i]);
    const QString logPath = QDir(QCoreApplication::applicationDirPath())
                                .filePath(QStringLiteral("panel-qtest.log"));
    QFile::remove(logPath);
    args << QStringLiteral("-o") << logPath + QStringLiteral(",txt");

    UiPanelTest testCase;
    const int failures = QTest::qExec(&testCase, args);

    QFile logFile(logPath);
    if (logFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const QByteArray data = logFile.readAll();
        logFile.close();
        std::fwrite(data.constData(), 1, size_t(data.size()), stderr);
        std::fflush(stderr);
    }
    QFile::remove(logPath);
    return failures;
}

#include "test_ui_panel.moc"
