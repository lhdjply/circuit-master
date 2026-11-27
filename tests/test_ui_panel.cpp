#include "core/Circuit.h"
#include "core/Netlist.h"
#include "csm_platform.h"
#include "csm_version.h"
#include "ui/ComponentItem.h"
#include "core/Component.h"
#include "core/Language.h"
#include "ui/I18n.h"
#include "ui/MainWindow.h"
#include "ui/SettingsDialog.h"
#include "ui/Theme.h"
#include "ui/MultimeterWindow.h"
#include "ui/PropertyPanel.h"
#include "ui/SchematicScene.h"
#include "ui/SchematicView.h"
#include "ui/Symbols.h"
#include "ui/UpdateDialog.h"
#include "ui/WaveformWindow.h"
#include "ui/WireItem.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QDataStream>
#include <QImage>
#include <QComboBox>
#include "ui/Dialogs.h"
#include <QMenuBar>
#include <QKeyEvent>
#include <QToolButton>
#include <QTableWidget>
#include <QKeySequenceEdit>
#include <QDir>
#include "core/Autosave.h"
#include <QJsonObject>
#include <QJsonArray>
#include "core/Shortcuts.h"
#include <QSettings>
#include <QFile>
#include <QIcon>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMimeData>
#include <QPixmap>
#include <QEvent>
#include <QPlainTextEdit>
#include <QMetaObject>
#include <QStyleHints>
#include <QPushButton>
#include <QSet>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTest>
#include <QTimer>

#include <memory>
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
    void themeSwitchesLightAndDark();
    void themeFollowsSystemSchemeChanges();
    void logicGateIconsDiffer();
    void libraryDragPixmapHasNoSelectionBackground();
    void signalGeneratorRowsFollowWaveform();
    void settingsDialogEditsShortcuts();
    void savedShortcutsApplyAtStartup();
    void keyHintsFollowShortcuts();
    void holdingAKeyRecordsOneShortcut();
    void conflictingShortcutWarnsAndRestores();
    void autosaveWritesRecoveryForDirtyDocuments();
    void autosaveSavesNamedDocumentsBackToDisk();
    void infoDialogsCannotBeResized();
    void framelessWindowHasCustomTitleBar();
    void settingsDialogHasCustomHeaderAndStaysResizable();
    void waveMonitorUsesCustomHeader();
    void multimeterIsFramelessAndFixedSize();
    void discardingTabThenRunningExampleStillStartsLive();
    void draggingTheTopRowMovesTheWindow();

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
    csm::saveLanguage(csm::Language::Chinese);   // 断言按中文写的
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

void UiPanelTest::themeFollowsSystemSchemeChanges()
{
    // 跟随系统时，系统深浅色一变就要整体重刷；固定浅色 / 深色时不响应。
    // offscreen 平台不会自己发这个信号，这里直接把信号打出去验证链路。
    int notified = 0;
    Theme::setChangeHandler([&notified] { ++notified; });

    Theme::save(Theme::Mode::System);
    Theme::apply(*qApp);
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    // Qt 6.4（Debian 12）既没有 Qt::ColorScheme，也没有 colorSchemeChanged 信号，
    // 这一段只能整段跳过 —— 下面的 ThemeChange 那条与版本无关，仍然会跑。
    QMetaObject::invokeMethod(QGuiApplication::styleHints(), "colorSchemeChanged",
                              Q_ARG(Qt::ColorScheme, Qt::ColorScheme::Dark));
    QCOMPARE(notified, 1);

    Theme::save(Theme::Mode::Light);
    QMetaObject::invokeMethod(QGuiApplication::styleHints(), "colorSchemeChanged",
                              Q_ARG(Qt::ColorScheme, Qt::ColorScheme::Dark));
    QCOMPARE(notified, 1); // 固定模式不响应
#endif

    // 平台发 QEvent::ThemeChange 也要能触发（这条与 Qt 版本无关，
    // Debian 12 的 Qt 6.4 上没有 colorSchemeChanged 信号，靠的就是它）
    Theme::save(Theme::Mode::System);
    Theme::apply(*qApp);
    MainWindow window;
    window.show();
    waitShown(window);
    int themeHits = 0;
    Theme::setChangeHandler([&themeHits] { ++themeHits; });
    QEvent themeEvent(QEvent::ThemeChange);
    QCoreApplication::sendEvent(&window, &themeEvent);
    settle();
    QCOMPARE(themeHits, 1);

    Theme::setChangeHandler(nullptr); // 别把捕获局部变量的 lambda 留着
}

void UiPanelTest::themeSwitchesLightAndDark()
{
    // 深浅色：模式存 QSettings，切换后调色板与「写死过样式」的控件都要跟着变
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    waitShown(window);

    const QColor lightWindow = QColor(0xf5, 0xf7, 0xfa);

    Theme::save(Theme::Mode::Light);
    Theme::apply(*qApp);
    settle();
    QVERIFY(!Theme::isDark());
    QCOMPARE(qApp->palette().color(QPalette::Window), lightWindow);
    const QColor lightCanvas = Theme::canvas();

    Theme::save(Theme::Mode::Dark);
    Theme::apply(*qApp);
    settle();
    QVERIFY(Theme::isDark());
    QVERIFY2(qApp->palette().color(QPalette::Window) != lightWindow,
             "切到深色后窗口底色应当变化");
    QVERIFY2(Theme::canvas() != lightCanvas, "画布底色也应当变化");
    // 深色下画布是暗的（亮度低于 40%），浅色下是亮的
    QVERIFY2(Theme::canvas().lightness() < 100, qPrintable(Theme::canvas().name()));
    QVERIFY2(lightCanvas.lightness() > 200, qPrintable(lightCanvas.name()));

    // 写死过样式的控件（消息区）也要跟着重建 —— 它走的是 Theme::styleWidget
    QPlainTextEdit* log = nullptr;
    for (QPlainTextEdit* edit : window.findChildren<QPlainTextEdit*>()) {
        if (edit->isReadOnly() && edit->toPlainText().contains(QStringLiteral("求解器")))
            log = edit;
    }
    QVERIFY(log);
    const QString darkStyle = log->styleSheet();
    Theme::save(Theme::Mode::Light);
    Theme::apply(*qApp);
    settle();
    QVERIFY2(log->styleSheet() != darkStyle, "切换主题后消息区的样式应当重建");

    // 来回切多次也要对：第一版按 WA_SetPalette 过滤子控件，
    // 结果深色能切过去、切回浅色却回不来（setPalette 自己会打上这个属性）
    for (int round = 0; round < 2; ++round) {
        Theme::save(Theme::Mode::Dark);
        Theme::apply(*qApp);
        settle();
        QCOMPARE(qApp->palette().color(QPalette::Window).name(), QStringLiteral("#111827"));
        Theme::save(Theme::Mode::Light);
        Theme::apply(*qApp);
        settle();
        QCOMPARE(qApp->palette().color(QPalette::Window).name(), lightWindow.name());
    }

    // save() 立刻改内存里的模式（落盘依赖可写的配置目录，测试环境里不一定有，
    // 所以这里只验内存状态，不去读文件）
    Theme::save(Theme::Mode::Dark);
    QCOMPARE(Theme::mode(), Theme::Mode::Dark);
    QVERIFY(Theme::isDark());

    Theme::save(Theme::Mode::Light); // 别把深色留给后面的用例
    QVERIFY(!Theme::isDark());
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
    QCOMPARE(counts.value(QStringLiteral("半导体")), 8);      // 二极管、稳压管、LED、三极管、MOS、IGBT
    QCOMPARE(counts.value(QStringLiteral("模拟 IC")), 4);     // 运放 / TL431 / 光耦 / OB2362
    QCOMPARE(counts.value(QStringLiteral("数字逻辑")), 5);     // 与 / 或 / 非 / 异或 / 同或
    QCOMPARE(counts.value(QStringLiteral("电源")), 3);   // 电压源 / 电流源 / 信号发生器        // 电压源 / 电流源
    QCOMPARE(counts.value(QStringLiteral("开关与接地")), 4);  // 开关 / 按钮开关 / 接地 / 网络标签
    QCOMPARE(counts.value(QStringLiteral("测量")), 4);        // TP / IP / 万用表 / 测量探针

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


void UiPanelTest::signalGeneratorRowsFollowWaveform()
{
    // 占空比只对方波有意义。从正弦切到方波时要立刻出现，
    // 不能等重新选中元件才发现 —— 之前就是切完还得点一下元件才看得到。
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    waitShown(window);

    SchematicScene* scene = window.findChild<SchematicScene*>();
    QVERIFY(scene);
    auto* item = scene->placeComponent(ComponentType::SignalGenerator, QPointF(300, 300));
    QVERIFY(item);
    item->setSelected(true);
    settle();

    auto* waveform = window.findChild<QComboBox*>(QStringLiteral("waveformCombo"));
    auto* duty = window.findChild<QLineEdit*>(QStringLiteral("dutyEdit"));
    QVERIFY(waveform);
    QVERIFY(duty);

    // 默认正弦：占空比那行不显示
    QCOMPARE(item->component().waveform, Waveform::Sine);
    QVERIFY2(!duty->isVisible(), "正弦时不该显示占空比");

    // 切到方波：占空比立刻出现
    waveform->setCurrentIndex(waveform->findData(int(Waveform::Square)));
    settle();
    QCOMPARE(item->component().waveform, Waveform::Square);
    QVERIFY2(duty->isVisible(), "切到方波后应当立刻显示占空比");

    // 再切回三角波：又收起来
    waveform->setCurrentIndex(waveform->findData(int(Waveform::Triangle)));
    settle();
    QCOMPARE(item->component().waveform, Waveform::Triangle);
    QVERIFY2(!duty->isVisible(), "三角波时不该显示占空比");
}


void UiPanelTest::settingsDialogEditsShortcuts()
{
    // 设置里的快捷键页：表格里改完点确定，要把结果交给主窗口
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    waitShown(window);

    QVector<SettingsDialog::ShortcutRow> rows = {
        {QStringLiteral("file.new"), QStringLiteral("New"),
         QKeySequence(QStringLiteral("Ctrl+N")), QKeySequence(QStringLiteral("Ctrl+N"))},
        {QStringLiteral("edit.rotate"), QStringLiteral("Rotate"),
         QKeySequence(QStringLiteral("Space")), QKeySequence(QStringLiteral("Space"))},
    };
    SettingsDialog dialog(&window, [] {}, [] {}, rows);
    dialog.show();
    settle();

    QVERIFY(dialog.findChild<QTabWidget*>(QStringLiteral("settingsTabs")));
    auto* table = dialog.findChild<QTableWidget*>(QStringLiteral("shortcutTable"));
    QVERIFY(table);
    QCOMPARE(table->rowCount(), rows.size());

    auto* edit = dialog.findChild<QKeySequenceEdit*>(QStringLiteral("shortcut_edit.rotate"));
    QVERIFY(edit);
    QCOMPARE(edit->keySequence(), QKeySequence(QStringLiteral("Space")));

    // 改成 Ctrl+R 再点确定
    edit->setKeySequence(QKeySequence(QStringLiteral("Ctrl+R")));
    settle();
    for (QPushButton* button : dialog.findChildren<QPushButton*>()) {
        if (button->text() == QStringLiteral("确定")) {
            button->click();
            break;
        }
    }
    settle();

    const QHash<QString, QKeySequence> edited = dialog.shortcuts();
    QCOMPARE(edited.size(), rows.size());
    QCOMPARE(edited.value(QStringLiteral("edit.rotate")), QKeySequence(QStringLiteral("Ctrl+R")));
    QCOMPARE(edited.value(QStringLiteral("file.new")), QKeySequence(QStringLiteral("Ctrl+N")));

    // 行尾的「×」：清空这一行的快捷键（= 不绑定）
    auto* clear = dialog.findChild<QToolButton*>(QStringLiteral("clear_edit.rotate"));
    QVERIFY2(clear, "每行都该有一个清空按钮");
    clear->click();
    settle();
    QVERIFY(edit->keySequence().isEmpty());
    for (QPushButton* button : dialog.findChildren<QPushButton*>()) {
        if (button->text() == QStringLiteral("确定")) {
            button->click();
            break;
        }
    }
    settle();
    QVERIFY(dialog.shortcuts().value(QStringLiteral("edit.rotate")).isEmpty());

    // 「恢复默认」把改动撤回默认值
    SettingsDialog second(&window, [] {}, [] {}, rows);
    second.show();
    settle();
    auto* edit2 = second.findChild<QKeySequenceEdit*>(QStringLiteral("shortcut_edit.rotate"));
    QVERIFY(edit2);
    edit2->setKeySequence(QKeySequence(QStringLiteral("Ctrl+Shift+R")));
    settle();
    auto* reset = second.findChild<QPushButton*>(QStringLiteral("resetShortcuts"));
    QVERIFY(reset);
    reset->click();
    settle();
    QCOMPARE(edit2->keySequence(), QKeySequence(QStringLiteral("Space")));
}


void UiPanelTest::savedShortcutsApplyAtStartup()
{
    // 存过覆盖之后，新建的窗口应当直接按新键绑定 —— 这是「改完能生效」的关键一环
    QCoreApplication::setOrganizationName(QStringLiteral("CircuitMasterTest"));
    QCoreApplication::setApplicationName(QStringLiteral("CircuitMasterTest"));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, QDir::tempPath());
    Shortcuts::clearAll();

    Shortcuts::save(QStringLiteral("file.new"), QStringLiteral("Ctrl+Shift+N"),
                    QStringLiteral("Ctrl+N"));

    MainWindow window;
    window.resize(1200, 800);
    window.show();
    waitShown(window);

    QAction* newAction = nullptr;
    for (QAction* action : window.findChildren<QAction*>()) {
        if (action->text().startsWith(QStringLiteral("新建")))
            newAction = action;
    }
    QVERIFY(newAction);
    QCOMPARE(newAction->shortcut(), QKeySequence(QStringLiteral("Ctrl+Shift+N")));

    // 清掉覆盖后回到默认
    Shortcuts::clearAll();
    MainWindow plain;
    plain.show();
    waitShown(plain);
    for (QAction* action : plain.findChildren<QAction*>()) {
        if (action->text().startsWith(QStringLiteral("新建")))
            newAction = action;
    }
    QCOMPARE(newAction->shortcut(), QKeySequence(QStringLiteral("Ctrl+N")));
    QFile::remove(QSettings().fileName());
}


void UiPanelTest::keyHintsFollowShortcuts()
{
    // 提示文字里的按键取自真实快捷键：改过设置后不能再写死「空格」「F5」
    QCoreApplication::setOrganizationName(QStringLiteral("CircuitMasterTest"));
    QCoreApplication::setApplicationName(QStringLiteral("CircuitMasterTest"));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, QDir::tempPath());
    Shortcuts::clearAll();
    Shortcuts::save(QStringLiteral("edit.rotate"), QStringLiteral("Ctrl+R"),
                    QStringLiteral("Space"));

    MainWindow window;
    window.resize(1200, 800);
    window.show();
    waitShown(window);

    // 状态栏（选择模式）里的旋转提示应当是新的键。
    // 比较用的文本要用 NativeText 现算：macOS 上 Ctrl+R 显示成「⌘R」，
    // 写死 "Ctrl+R" 在那边必然失败
    const QString rebound = QKeySequence(QStringLiteral("Ctrl+R")).toString(QKeySequence::NativeText);
    const QString defaultKey = QKeySequence(Qt::Key_Space).toString(QKeySequence::NativeText);
    bool found = false;
    for (QLabel* label : window.findChildren<QLabel*>()) {
        if (!label->text().contains(QStringLiteral("旋转"))
            || !label->text().contains(QStringLiteral("选择模式"))) {
            continue;
        }
        found = true;
        QVERIFY2(label->text().contains(rebound), qPrintable(label->text()));
        QVERIFY2(!label->text().contains(defaultKey), qPrintable(label->text()));
    }
    QVERIFY2(found, "没找到状态栏的选择模式提示");

    Shortcuts::clearAll();
    QFile::remove(QSettings().fileName());
}


void UiPanelTest::holdingAKeyRecordsOneShortcut()
{
    // 按住一个键不放时，平台会不停送自动重复的 keyPress；
    // 默认的 QKeySequenceEdit 会把它记成 "Ctrl+R, Ctrl+R, …"，看起来像一直在录。
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    waitShown(window);

    SettingsDialog dialog(&window, [] {}, [] {},
                          {{QStringLiteral("edit.rotate"), QStringLiteral("Rotate"),
                            QKeySequence(QStringLiteral("Space")),
                            QKeySequence(QStringLiteral("Space"))}});
    dialog.show();
    settle();
    auto* edit = dialog.findChild<QKeySequenceEdit*>(QStringLiteral("shortcut_edit.rotate"));
    QVERIFY(edit);
    edit->setFocus();
    settle();

    const auto send = [edit](bool autoRepeat) {
        auto* event = new QKeyEvent(QEvent::KeyPress, Qt::Key_R, Qt::ControlModifier,
                                    QStringLiteral("R"), autoRepeat);
        QCoreApplication::sendEvent(edit, event);
        delete event;
    };
    send(false);                    // 真正按下的一次
    for (int i = 0; i < 5; ++i)     // 之后的自动重复
        send(true);
    settle();

    QCOMPARE(edit->keySequence(), QKeySequence(QStringLiteral("Ctrl+R")));
    QCOMPARE(edit->keySequence().count(), 1);
}


void UiPanelTest::conflictingShortcutWarnsAndRestores()
{
    // 改到与别的命令相同的组合：当场提示是谁占了，并把这一行还原成原来的按键
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    waitShown(window);

    SettingsDialog dialog(&window, [] {}, [] {},
                          {{QStringLiteral("edit.rotate"), QStringLiteral("Rotate"),
                            QKeySequence(QStringLiteral("Space")),
                            QKeySequence(QStringLiteral("Space"))},
                           {QStringLiteral("sim.run"), QStringLiteral("Run"),
                            QKeySequence(QStringLiteral("F5")),
                            QKeySequence(QStringLiteral("F5"))}});
    dialog.show();
    settle();
    auto* tabs = dialog.findChild<QTabWidget*>(QStringLiteral("settingsTabs"));
    QVERIFY(tabs);
    // 切到快捷键页（按名字找，别按序号 —— 页签顺序会变）
    tabs->setCurrentWidget(dialog.findChild<QWidget*>(QStringLiteral("shortcutsTab")));
    settle();

    auto* rotate = dialog.findChild<QKeySequenceEdit*>(QStringLiteral("shortcut_edit.rotate"));
    auto* warning = dialog.findChild<QLabel*>(QStringLiteral("shortcutWarning"));
    QVERIFY(rotate);
    QVERIFY(warning);
    rotate->setFocus();
    settle();

    // 按 F5（已被「运行」占用）
    auto* conflict = new QKeyEvent(QEvent::KeyPress, Qt::Key_F5, Qt::NoModifier,
                                   QString(), false);
    QCoreApplication::sendEvent(rotate, conflict);
    delete conflict;
    settle();

    QCOMPARE(rotate->keySequence(), QKeySequence(QStringLiteral("Space")));   // 还原
    QVERIFY2(warning->isVisible(), "冲突时必须提示");
    QVERIFY2(warning->text().contains(QStringLiteral("Run")), qPrintable(warning->text()));

    // 换成不冲突的键：警告收起，改动留下
    auto* ok = new QKeyEvent(QEvent::KeyPress, Qt::Key_R, Qt::ControlModifier,
                             QStringLiteral("R"), false);
    QCoreApplication::sendEvent(rotate, ok);
    delete ok;
    settle();
    QCOMPARE(rotate->keySequence(), QKeySequence(QStringLiteral("Ctrl+R")));
    QVERIFY2(!warning->isVisible(), "冲突解除后提示应当收起");
}


void UiPanelTest::autosaveWritesRecoveryForDirtyDocuments()
{
    // 有改动 → 写一份恢复副本；关掉开关 → 不写；存回文件 → 副本消失
    const QString dataHome = QDir::tempPath() + QStringLiteral("/csm-autosave-ui");
    QDir(dataHome).removeRecursively();
    qputenv("XDG_DATA_HOME", dataHome.toUtf8());
    QCoreApplication::setOrganizationName(QStringLiteral("CircuitMasterTest"));
    QCoreApplication::setApplicationName(QStringLiteral("CircuitMasterTest"));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, QDir::tempPath());
    Autosave::clearAll();
    Autosave::setEnabled(true);

    MainWindow window;
    window.resize(1200, 800);
    window.show();
    waitShown(window);

    auto* scene = window.findChild<SchematicScene*>();
    QVERIFY(scene);
    scene->placeComponent(ComponentType::Resistor, QPointF(300.0, 300.0));
    settle();

    window.writeRecoveryCopies();
    QCOMPARE(Autosave::pending().size(), 1);
    QCOMPARE(Autosave::pending().first().circuit.value(QStringLiteral("components")).toArray().size(), 1);

    // 关掉「崩溃恢复」后不再写副本
    Autosave::setRecoveryEnabled(false);
    Autosave::clearAll();
    window.writeRecoveryCopies();
    QVERIFY(Autosave::pending().isEmpty());
    Autosave::setRecoveryEnabled(true);

    Autosave::setEnabled(true);
    Autosave::clearAll();
    QDir(dataHome).removeRecursively();
    QFile::remove(QSettings().fileName());
}


void UiPanelTest::autosaveSavesNamedDocumentsBackToDisk()
{
    // 已经存过盘的图纸，自动保存应当直接写回原文件（等价于 Ctrl+S），
    // 而不是只留一份副本；未命名的才走恢复副本
    const QString dataHome = QDir::tempPath() + QStringLiteral("/csm-autosave-named");
    QDir(dataHome).removeRecursively();
    qputenv("XDG_DATA_HOME", dataHome.toUtf8());
    QCoreApplication::setOrganizationName(QStringLiteral("CircuitMasterTest"));
    QCoreApplication::setApplicationName(QStringLiteral("CircuitMasterTest"));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, QDir::tempPath());
    Autosave::clearAll();
    Autosave::setEnabled(true);

    const QString path = QDir::tempPath() + QStringLiteral("/csm-autosave-named.csm");
    QFile::remove(path);
    QVERIFY(QFile::copy(QStringLiteral(":/examples/01_rc_charge.csm"), path));
    // 资源文件是只读的，复制出来的文件沿用了只读权限 —— 给它补上写权限
    QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                    | QFileDevice::ReadUser | QFileDevice::WriteUser);

    MainWindow window;
    window.resize(1200, 800);
    window.show();
    waitShown(window);
    QVERIFY(window.openPath(path));
    settle();

    auto* scene = window.findChild<SchematicScene*>();
    QVERIFY(scene);
    const int before = scene->componentItems().size();
    scene->placeComponent(ComponentType::Resistor, QPointF(700.0, 560.0));
    settle();
    QVERIFY2(window.windowTitle().contains(QStringLiteral("*")), "改过之后标题应当带星号");

    window.autosaveNow();
    settle();

    // 文件里应当多了那个电阻
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    file.close();
    QCOMPARE(root.value(QStringLiteral("components")).toArray().size(), before + 1);
    // 存回之后不再算「未保存」，也不留恢复副本
    QVERIFY2(!window.windowTitle().contains(QStringLiteral("*")), "自动保存后不该再带星号");
    QVERIFY(Autosave::pending().isEmpty());

    QFile::remove(path);
    QDir(dataHome).removeRecursively();
    QFile::remove(QSettings().fileName());
}


void UiPanelTest::infoDialogsCannotBeResized()
{
    // 「保存更改 / 快速上手 / 关于 / 更新日志」这几个提示框内容固定，
    // 不该让用户拖大小：最小尺寸与最大尺寸相等，拖动也改不掉。
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    waitShown(window);

    // 提示框是模态的（exec 阻塞），只能靠定时器在它弹出后测量。
    // 结果放进 shared_ptr：定时器的 lambda 活得比这个作用域长，
    // 捕获局部变量的引用会在函数返回后写野指针。
    struct Probe {
        bool seen = false;
        bool fixed = true;
        int windowType = 0;
        QSize before;
        QSize after;
    };
    const auto probe = std::make_shared<Probe>();
    const auto arm = [probe] {
        *probe = Probe{};
        QTimer::singleShot(200, [probe] {
            QWidget* dialog = QApplication::activeModalWidget();
            if (!dialog)
                return;
            probe->seen = true;
            probe->windowType = int(dialog->windowFlags() & Qt::WindowType_Mask);
            probe->before = dialog->size();
            probe->fixed = dialog->minimumSize() == dialog->maximumSize();
            dialog->resize(probe->before + QSize(300, 200));   // 试着拖大
            probe->after = dialog->size();
            dialog->close();
        });
    };
    const auto verify = [probe](const char* what) {
        QVERIFY2(probe->seen, what);
        QVERIFY2(probe->fixed, what);
        QCOMPARE(probe->after, probe->before);
        // Tool 类型：带父窗口时任务栏里不会再列一条
        QCOMPARE(probe->windowType, int(Qt::Tool));
    };

    arm();
    Dialogs::askSaveDiscardCancel(&window, QStringLiteral("保存更改"),
                                  QStringLiteral("「未命名」有未保存的更改，是否保存？"));
    verify("保存更改");

    arm();
    Dialogs::guide(&window, QStringLiteral("快速上手"),
                   QStringLiteral("<b>三步完成一次仿真</b>"));
    verify("快速上手");

    arm();
    Dialogs::about(&window, QStringLiteral("v0.5.1"));
    verify("关于");

    arm();
    Dialogs::changelog(&window);
    verify("更新日志");
}


void UiPanelTest::framelessWindowHasCustomTitleBar()
{
    // 系统那条标题栏去掉了，换成菜单行右上角的「标题 + 三个按钮」
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    waitShown(window);

    QVERIFY2(window.windowFlags().testFlag(Qt::FramelessWindowHint), "窗口应当无边框");
    QVERIFY2(window.menuBar() != nullptr, "菜单栏不能因为自绘标题栏而丢掉");

    auto* titleBar = window.findChild<QWidget*>(QStringLiteral("titleBar"));
    QVERIFY(titleBar);
    // 左上角的软件图标
    auto* mark = window.findChild<QLabel*>(QStringLiteral("titleBarIcon"));
    QVERIFY2(mark != nullptr, "左上角应当有软件图标");
    QVERIFY2(!mark->pixmap().isNull(), "图标没有加载出来");

    for (const char* name : {"windowMinimize", "windowMaximize", "windowClose"})
        QVERIFY2(window.findChild<QWidget*>(QString::fromLatin1(name)) != nullptr, name);

    // 顶栏不再重复显示窗口标题，只留按钮
    QVERIFY2(window.findChild<QLabel*>(QStringLiteral("titleBarTitle")) == nullptr,
             "顶栏不该再有标题文字");

    // 最大化时按钮换成「还原」：这里只验证状态切换不崩、按钮还在
    window.showMaximized();
    settle();
    QVERIFY(window.findChild<QWidget*>(QStringLiteral("windowMaximize")) != nullptr);
    window.showNormal();
    settle();
}


void UiPanelTest::draggingTheTopRowMovesTheWindow()
{
    MainWindow window;
    window.resize(1100, 760);
    window.show();
    waitShown(window);
    settle();

    QMenuBar* bar = window.menuBar();
    QVERIFY(bar);
    // 菜单项右侧的空白区：那里既不是菜单、也不是窗口按钮
    const QPoint blank(bar->width() - 260, bar->height() / 2);
    QVERIFY2(!bar->actionAt(blank), "取样点应当落在空白处");

    const QPoint start = window.pos();
    const auto send = [&](QEvent::Type type, const QPoint& at, Qt::MouseButtons buttons,
                          Qt::MouseButton button) {
        const QPoint global = bar->mapToGlobal(at);
        QMouseEvent event(type, QPointF(at), QPointF(global), button, buttons, Qt::NoModifier);
        QCoreApplication::sendEvent(bar, &event);
    };

    // 按住拖 40×30：窗口应当跟着走（offscreen 平台没有窗口管理器，
    // 走的是 startSystemMove() 失败后的手动搬运）
    send(QEvent::MouseButtonPress, blank, Qt::LeftButton, Qt::LeftButton);
    send(QEvent::MouseMove, blank + QPoint(40, 30), Qt::LeftButton, Qt::NoButton);
    send(QEvent::MouseButtonRelease, blank + QPoint(40, 30), Qt::NoButton, Qt::LeftButton);
    settle();
    QCOMPARE(window.pos(), start + QPoint(40, 30));

    // 双击同一片空白：最大化 / 还原
    window.showNormal();
    settle();
    send(QEvent::MouseButtonDblClick, blank, Qt::LeftButton, Qt::LeftButton);
    settle();
    QVERIFY2(window.isMaximized(), "双击顶部应当最大化");
    const QPoint maximizedPos = window.pos();
    Q_UNUSED(maximizedPos);

    send(QEvent::MouseButtonDblClick, blank, Qt::LeftButton, Qt::LeftButton);
    settle();
    QVERIFY2(!window.isMaximized(), "再双击应当还原");

    window.close();
}


void UiPanelTest::settingsDialogHasCustomHeaderAndStaysResizable()
{
    // 设置窗口也换了自绘顶栏，但它要能拉大小，所以不能像提示框那样锁死
    MainWindow window;
    window.show();
    waitShown(window);

    SettingsDialog dialog(&window, [] {}, [] {}, {});
    dialog.show();
    settle();

    QVERIFY2(dialog.windowFlags().testFlag(Qt::FramelessWindowHint), "设置窗口应当无边框");
    auto* header = dialog.findChild<QWidget*>(QStringLiteral("dialogHeader"));
    QVERIFY(header);
    QVERIFY(dialog.findChild<QWidget*>(QStringLiteral("dialogHeaderClose")) != nullptr);

    // 可调：最小尺寸与最大尺寸不相等，改大之后确实变大
    QVERIFY(dialog.minimumSize() != dialog.maximumSize());
    const QSize before = dialog.size();
    dialog.resize(before + QSize(80, 60));
    settle();
    QCOMPARE(dialog.size(), before + QSize(80, 60));
}


void UiPanelTest::waveMonitorUsesCustomHeader()
{
    // 波形监控是独立窗口，顶部同样换成自绘的
    MainWindow window;
    window.resize(1100, 760);
    window.show();
    waitShown(window);

    window.openScope();
    settle();

    auto* monitor = window.findChild<QWidget*>(QStringLiteral("waveMonitorWindow"));
    QVERIFY(monitor);
    QVERIFY2(monitor->windowFlags().testFlag(Qt::FramelessWindowHint), "波形监控应当无边框");
    QVERIFY(monitor->findChild<QWidget*>(QStringLiteral("dialogHeader")) != nullptr);
    QVERIFY(monitor->findChild<QWidget*>(QStringLiteral("dialogHeaderClose")) != nullptr);
    // Tool：不在任务栏里单列，靠「波形监控 / F6」开关
    QCOMPARE(int(monitor->windowFlags() & Qt::WindowType_Mask), int(Qt::Tool));

    monitor->close();
    settle();
}


void UiPanelTest::multimeterIsFramelessAndFixedSize()
{
    // 万用表是浮在画布上的小窗：同样自绘顶栏，但尺寸固定 —— 拉不动，也不能最大化
    MultimeterWindow meter(QStringLiteral("R1"), MultimeterMode::Voltage);
    meter.show();
    settle();

    QVERIFY2(meter.windowFlags().testFlag(Qt::FramelessWindowHint), "万用表应当无边框");
    QVERIFY(meter.findChild<QWidget*>(QStringLiteral("dialogHeader")) != nullptr);
    QCOMPARE(meter.minimumSize(), meter.maximumSize());   // 固定尺寸 = 无法拉伸

    const QSize before = meter.size();
    meter.resize(before + QSize(120, 80));
    settle();
    QCOMPARE(meter.size(), before);

    // 没有最大化位：窗口管理器不会提供最大化动作
    QVERIFY2(!meter.windowFlags().testFlag(Qt::WindowMaximizeButtonHint),
             "万用表不该有最大化按钮");
    // Tool 类型：不在任务栏里单列一条
    QCOMPARE(int(meter.windowFlags() & Qt::WindowType_Mask), int(Qt::Tool));

    meter.close();
}


void UiPanelTest::discardingTabThenRunningExampleStillStartsLive()
{
    // 关闭标签页（选丢弃）会销毁该图纸的动态示波器。之后把示例载进同一个文档时
    // 必须重新绑一份，否则 startLiveScope() 会被跳过：按钮停在「运行仿真」，
    // 波形监控也一直是灰的。
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    waitShown(window);

    auto* scene = window.findChild<SchematicScene*>();
    QVERIFY(scene);
    scene->placeComponent(ComponentType::Resistor, QPointF(300.0, 300.0));
    settle();

    // 关闭标签页 → 在弹窗里选「丢弃」
    QTimer::singleShot(200, [] {
        QWidget* dialog = QApplication::activeModalWidget();
        if (!dialog)
            return;
        for (QPushButton* button : dialog->findChildren<QPushButton*>()) {
            if (button->text().contains(QStringLiteral("丢弃"))) {
                button->click();
                return;
            }
        }
        dialog->close();
    });
    const auto findAction = [&window](const QString& text) -> QAction* {
        for (QAction* action : window.findChildren<QAction*>()) {
            QString plain = action->text();
            plain.remove(QLatin1Char('&'));
            if (plain.startsWith(text))
                return action;
        }
        return nullptr;
    };
    QAction* closeTab = findAction(QStringLiteral("关闭标签页"));
    QVERIFY(closeTab);
    closeTab->trigger();
    settle();

    // 打开 TL431 示例
    QMenu* examples = nullptr;
    for (QMenu* menu : window.menuBar()->findChildren<QMenu*>()) {
        if (menu->title().contains(QStringLiteral("示例")))
            examples = menu;
    }
    QVERIFY(examples);
    QAction* tl431 = nullptr;
    for (QAction* action : examples->actions()) {
        if (action->text().contains(QStringLiteral("TL431")))
            tl431 = action;
    }
    QVERIFY(tl431);
    tl431->trigger();
    settle();

    QAction* run = findAction(QStringLiteral("运行仿真"));
    QVERIFY(run);
    run->trigger();
    settle();

    // 连续仿真真的起来了：按钮变「停止仿真」，波形监控可用
    QCOMPARE(run->text(), CSM_TR("Stop Simulation"));
    QAction* monitor = findAction(QStringLiteral("波形监控"));
    QVERIFY(monitor);
    QVERIFY2(monitor->isEnabled(), "仿真跑起来后波形监控应当可用");

    run->trigger();   // 收尾：停掉，免得定时器在别的用例里继续跑
    settle();
    QCOMPARE(run->text(), CSM_TR("Run Simulation"));
}

#include "test_ui_panel.moc"
