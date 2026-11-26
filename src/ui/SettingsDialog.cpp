#include "ui/SettingsDialog.h"

#include "core/Grid.h"
#include <QSpinBox>
#include <QCheckBox>
#include "core/Autosave.h"
#include "ui/DialogFrame.h"
#include <QtGlobal>
#include "core/Language.h"
#include "ui/Dialogs.h"
#include "ui/Theme.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTabWidget>
#include <QTableWidget>
#include <QToolButton>

#include <functional>
#include <QVBoxLayout>

namespace csm {

namespace {

/// 只收一个组合键的 QKeySequenceEdit。
///
/// 默认的 QKeySequenceEdit 在长按一个键时，平台的自动重复会不停送 keyPress，
/// 于是它把同一个组合记录成 "Ctrl+R, Ctrl+R, …"，看起来像「一直在录」。
/// 这里忽略自动重复事件，并且只保留最后一个组合。
class SingleKeySequenceEdit : public QKeySequenceEdit {
public:
    explicit SingleKeySequenceEdit(const QKeySequence& sequence, QWidget* parent = nullptr)
        : QKeySequenceEdit(sequence, parent)
    {
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
        setMaximumSequenceLength(1);
#endif
    }

    /// 收下按键后同步校验：返回的值会被直接写回输入框。
    /// 放在这里而不是 keySequenceChanged 里，是因为那个信号要等 1 秒的收尾定时器，
    /// 冲突提示会慢半拍。
    void setValidator(std::function<QKeySequence(const QKeySequence&)> validator)
    {
        m_validator = std::move(validator);
    }

protected:
    void keyPressEvent(QKeyEvent* event) override
    {
        if (event->isAutoRepeat()) {
            event->accept();   // 长按产生的重复按键不记录
            return;
        }
        QKeySequenceEdit::keyPressEvent(event);
        QKeySequence current = keySequence();
#if QT_VERSION < QT_VERSION_CHECK(6, 5, 0)
        if (current.count() > 1) {   // 6.4 没有 setMaximumSequenceLength，自己截断
            current = QKeySequence(current[current.count() - 1]);
            setKeySequence(current);
        }
#endif
        if (!m_validator)
            return;
        const QKeySequence fixed = m_validator(current);
        if (fixed != keySequence())
            setKeySequence(fixed);
    }

private:
    std::function<QKeySequence(const QKeySequence&)> m_validator;
};

QString themeLabel(Theme::Mode mode)
{
    switch (mode) {
    case Theme::Mode::System: return CSM_TR("Follow System");
    case Theme::Mode::Light:  return CSM_TR("Light");
    case Theme::Mode::Dark:   return CSM_TR("Dark");
    }
    return QString();
}

} // namespace

SettingsDialog::SettingsDialog(QWidget* parent, std::function<void()> applyTheme,
                               std::function<void()> applyGrid,
                               const QVector<ShortcutRow>& shortcuts,
                               std::function<void()> applyAutosave)
    : QDialog(parent)
    , m_originalTheme(int(Theme::mode()))
    , m_applyTheme(std::move(applyTheme))
    , m_applyGrid(std::move(applyGrid))
    , m_applyAutosave(std::move(applyAutosave))
    , m_rows(shortcuts)
{
    setWindowTitle(CSM_TR("Settings"));
    setModal(true);
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    // 快捷键表有 22 行，太小的默认尺寸会出现滚动条且显得挤
    resize(620, 560);
    setMinimumSize(520, 420);
    Dialogs::applyStyle(*this);   // 和其它提示框同一套观感

    // 和其它窗口一样用自绘顶栏；这个窗口要能拉大小，再挂上八向拖拽区
    auto* outer = makeFramelessShell(*this, CSM_TR("Settings"), QMargins(20, 16, 20, 14), 14);
    m_resizeGrips = new ResizeGrips(this);

    auto* tabs = new QTabWidget(this);
    tabs->setObjectName(QStringLiteral("settingsTabs"));
    outer->addWidget(tabs, 1);

    auto* appearance = new QWidget(tabs);
    appearance->setObjectName(QStringLiteral("appearanceTab"));
    auto* appearanceLayout = new QVBoxLayout(appearance);
    appearanceLayout->setContentsMargins(12, 12, 12, 8);
    appearanceLayout->setSpacing(12);
    tabs->addTab(appearance, CSM_TR("Appearance"));

    auto* form = new QFormLayout;
    form->setSpacing(10);
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);

    // 主题：只记下选择，点「确定」才生效（不然点进去随手翻一下界面就变了）
    auto* themeBox = new QComboBox(this);
    for (Theme::Mode mode : {Theme::Mode::System, Theme::Mode::Light, Theme::Mode::Dark})
        themeBox->addItem(themeLabel(mode), int(mode));
    themeBox->setCurrentIndex(themeBox->findData(m_originalTheme));
    form->addRow(CSM_TR("Theme"), themeBox);

    // 网格大小：同时决定画布小格、吸附步长与网表的连通判定
    auto* gridBox = new QComboBox(this);
    for (double value : Grid::kOptions)
        gridBox->addItem(QStringLiteral("%1 px").arg(value, 0, 'g', 3), value);
    gridBox->setCurrentIndex(qMax(0, gridBox->findData(Grid::size())));
    if (gridBox->currentData().toDouble() != Grid::size()) {
        // 手改过配置、不在预设档里时补一项，免得显示成错的值
        gridBox->addItem(QStringLiteral("%1 px").arg(Grid::size(), 0, 'g', 3), Grid::size());
        gridBox->setCurrentIndex(gridBox->count() - 1);
    }
    form->addRow(CSM_TR("Grid size"), gridBox);

    // 语言：只存盘，重启后生效（文案编译在程序里）
    auto* languageBox = new QComboBox(this);
    for (Language value : {Language::Chinese, Language::English})
        languageBox->addItem(languageDisplayName(value), int(value));
    languageBox->setCurrentIndex(languageBox->findData(int(language())));
    form->addRow(CSM_TR("Language"), languageBox);

    appearanceLayout->addLayout(form);

    auto* hint = new QLabel(CSM_TR("The interface language applies after a restart."), this);
    hint->setWordWrap(true);
    hint->setStyleSheet(QStringLiteral("color: %1; font-size: 11px;").arg(Theme::textMuted().name()));
    appearanceLayout->addWidget(hint);
    appearanceLayout->addStretch(1);

    // ---------------- 保存 ----------------
    auto* saving = new QWidget(tabs);
    saving->setObjectName(QStringLiteral("savingTab"));
    auto* savingLayout = new QVBoxLayout(saving);
    savingLayout->setContentsMargins(12, 12, 12, 8);
    savingLayout->setSpacing(12);
    tabs->addTab(saving, CSM_TR("Saving"));

    auto* autosaveBox = new QCheckBox(CSM_TR("Autosave"), saving);
    autosaveBox->setObjectName(QStringLiteral("autosaveEnabled"));
    autosaveBox->setChecked(Autosave::enabled());
    savingLayout->addWidget(autosaveBox);

    auto* intervalRow = new QHBoxLayout;
    intervalRow->addSpacing(22);
    intervalRow->addWidget(new QLabel(CSM_TR("Every"), saving));
    auto* intervalSpin = new QSpinBox(saving);
    intervalSpin->setObjectName(QStringLiteral("autosaveInterval"));
    intervalSpin->setRange(1, 120);
    intervalSpin->setSuffix(CSM_TR(" minutes"));
    intervalSpin->setValue(Autosave::intervalMinutes());
    intervalSpin->setEnabled(autosaveBox->isChecked());
    intervalRow->addWidget(intervalSpin);
    intervalRow->addStretch(1);
    savingLayout->addLayout(intervalRow);

    auto* recoveryBox = new QCheckBox(CSM_TR("Crash recovery"), saving);
    recoveryBox->setObjectName(QStringLiteral("recoveryEnabled"));
    recoveryBox->setChecked(Autosave::recoveryEnabled());
    savingLayout->addWidget(recoveryBox);

    auto* recoveryRow = new QHBoxLayout;
    recoveryRow->addSpacing(22);
    recoveryRow->addWidget(new QLabel(CSM_TR("Every"), saving));
    auto* recoverySpin = new QSpinBox(saving);
    recoverySpin->setObjectName(QStringLiteral("recoveryInterval"));
    recoverySpin->setRange(1, 60);
    recoverySpin->setSuffix(CSM_TR(" minutes"));
    recoverySpin->setValue(Autosave::recoveryIntervalMinutes());
    recoverySpin->setEnabled(recoveryBox->isChecked());
    recoveryRow->addWidget(recoverySpin);
    recoveryRow->addStretch(1);
    savingLayout->addLayout(recoveryRow);
    connect(recoveryBox, &QCheckBox::toggled, recoverySpin, &QSpinBox::setEnabled);
    savingLayout->addStretch(1);

    connect(autosaveBox, &QCheckBox::toggled, intervalSpin, &QSpinBox::setEnabled);

    // ---------------- 快捷键 ----------------
    auto* keys = new QWidget(tabs);
    keys->setObjectName(QStringLiteral("shortcutsTab"));
    auto* keysLayout = new QVBoxLayout(keys);
    keysLayout->setContentsMargins(12, 12, 12, 8);
    keysLayout->setSpacing(10);
    tabs->addTab(keys, CSM_TR("Shortcuts"));

    auto* keyHint = new QLabel(
        CSM_TR("Click a shortcut and press the new keys. The × at the end of a row clears "
               "it, leaving that command without a shortcut."),
        this);
    keyHint->setWordWrap(true);
    keyHint->setStyleSheet(QStringLiteral("color: %1; font-size: 11px;").arg(Theme::textMuted().name()));
    keysLayout->addWidget(keyHint);

    auto* table = new QTableWidget(qMax(1, m_rows.size()), 3, keys);
    table->setObjectName(QStringLiteral("shortcutTable"));
    table->setHorizontalHeaderLabels({CSM_TR("Command"), CSM_TR("Shortcut"), QString()});
    table->verticalHeader()->setVisible(false);
    table->setSelectionMode(QAbstractItemView::NoSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->horizontalHeader()->setStretchLastSection(false);
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Fixed);
    table->setColumnWidth(2, 30);
    for (int row = 0; row < m_rows.size(); ++row) {
        const ShortcutRow& entry = m_rows.at(row);
        auto* name = new QTableWidgetItem(entry.label);
        name->setData(Qt::UserRole, entry.id);
        table->setItem(row, 0, name);

        auto* edit = new SingleKeySequenceEdit(entry.current, table);
        edit->setObjectName(QStringLiteral("shortcut_%1").arg(entry.id));
        // 记录默认值，供「恢复默认」用
        edit->setProperty("defaultKey",
                          entry.defaultKey.toString(QKeySequence::PortableText));
        table->setCellWidget(row, 1, edit);

        m_currentKeys.insert(entry.id, entry.current);
        m_labels.insert(entry.id, entry.label);
        const QString id = entry.id;
        edit->setValidator([this, id](const QKeySequence& sequence) {
            return validateShortcut(id, sequence);
        });

        // 行尾的「×」：清空这一行的快捷键（= 不绑定），比在输入框里选中再删更直观
        auto* clear = new QToolButton(table);
        clear->setObjectName(QStringLiteral("clear_%1").arg(entry.id));
        clear->setText(QStringLiteral("×"));
        clear->setAutoRaise(true);
        clear->setCursor(Qt::PointingHandCursor);
        clear->setToolTip(CSM_TR("Clear this shortcut, leaving the command unbound"));
        connect(clear, &QToolButton::clicked, edit, [this, edit, id = entry.id] {
            edit->clear();
            m_currentKeys[id] = QKeySequence();
            if (m_shortcutWarning)
                m_shortcutWarning->hide();
        });
        auto* clearHolder = new QWidget(table);
        auto* clearLayout = new QHBoxLayout(clearHolder);
        clearLayout->setContentsMargins(0, 0, 0, 0);
        clearLayout->addWidget(clear, 0, Qt::AlignCenter);
        table->setCellWidget(row, 2, clearHolder);
    }
    table->verticalHeader()->setDefaultSectionSize(34);   // 行高舒展开，输入框不至于贴边
    keysLayout->addWidget(table, 1);

    m_shortcutWarning = new QLabel(keys);
    m_shortcutWarning->setObjectName(QStringLiteral("shortcutWarning"));
    m_shortcutWarning->setWordWrap(true);
    m_shortcutWarning->setStyleSheet(QStringLiteral("color: %1;").arg(Theme::danger().name()));
    m_shortcutWarning->hide();
    keysLayout->addWidget(m_shortcutWarning);

    auto* keyButtons = new QHBoxLayout;
    auto* resetButton = new QPushButton(CSM_TR("Restore Defaults"), keys);
    resetButton->setObjectName(QStringLiteral("resetShortcuts"));
    resetButton->setAutoDefault(false);
    keyButtons->addWidget(resetButton);
    keyButtons->addStretch(1);
    keysLayout->addLayout(keyButtons);

    connect(resetButton, &QPushButton::clicked, this, [this, table] {
        for (int row = 0; row < m_rows.size(); ++row) {
            auto* edit = qobject_cast<QKeySequenceEdit*>(table->cellWidget(row, 1));
            if (!edit)
                continue;
            const QString text = edit->property("defaultKey").toString();
            edit->setKeySequence(text.isEmpty() ? QKeySequence()
                                                : QKeySequence(text, QKeySequence::PortableText));
        }
    });

    // 普通按钮：QDialogButtonBox 会给确定 / 取消加上系统主题的绿勾、红叉图标，
    // 和本程序其它提示框（纯文字按钮）不一致
    auto* buttonRow = new QHBoxLayout;
    buttonRow->addStretch(1);
    auto* okButton = new QPushButton(CSM_TR("OK"), this);
    okButton->setDefault(true);
    okButton->setAutoDefault(true);
    auto* cancelButton = new QPushButton(CSM_TR("Cancel"), this);
    cancelButton->setAutoDefault(false);
    buttonRow->addWidget(okButton);
    buttonRow->addWidget(cancelButton);

    connect(okButton, &QPushButton::clicked, this,
            [this, themeBox, gridBox, languageBox, table, autosaveBox, intervalSpin,
             recoveryBox, recoverySpin] {
        const double grid = gridBox->currentData().toDouble();
        if (!qFuzzyCompare(grid, Grid::size())) {
            Grid::setSize(grid);
            if (m_applyGrid)
                m_applyGrid();
        }

        Autosave::setEnabled(autosaveBox->isChecked());
        Autosave::setIntervalMinutes(intervalSpin->value());
        Autosave::setRecoveryEnabled(recoveryBox->isChecked());
        Autosave::setRecoveryIntervalMinutes(recoverySpin->value());
        if (m_applyAutosave)
            m_applyAutosave();

        const Theme::Mode theme = Theme::Mode(themeBox->currentData().toInt());
        if (int(theme) != m_originalTheme) {
            Theme::save(theme);
            if (m_applyTheme)
                m_applyTheme();
        }

        // 快捷键：先收集，再查冲突 —— 同一个按键绑给两个命令，
        // Qt 只会随机触发其中一个，必须当场拦住
        QHash<QString, QKeySequence> collected;
        QHash<QString, QString> usedBy;
        for (int row = 0; row < m_rows.size(); ++row) {
            auto* edit = qobject_cast<QKeySequenceEdit*>(table->cellWidget(row, 1));
            if (!edit)
                continue;
            const QKeySequence sequence = edit->keySequence();
            const QString& id = m_rows.at(row).id;
            collected.insert(id, sequence);
            if (sequence.isEmpty())
                continue;
            const QString text = sequence.toString(QKeySequence::NativeText);
            if (usedBy.contains(text)) {
                QMessageBox::warning(
                    this, CSM_TR("Settings"),
                    CSM_TR("%1 is already used by %2; pick another shortcut.")
                        .arg(text, usedBy.value(text)));
                return;
            }
            usedBy.insert(text, m_rows.at(row).label);
        }

        const Language chosen = Language(languageBox->currentData().toInt());
        const bool changed = chosen != language();
        saveLanguage(chosen);
        m_edited = collected;
        accept();
        if (changed)
            Dialogs::info(parentWidget(), CSM_TR("Settings"),
                          CSM_TR("The interface language has been changed. "
                                 "Restart the application to apply it."));
    });
    connect(cancelButton, &QPushButton::clicked, this, &QDialog::reject);
    outer->addLayout(buttonRow);
}

QKeySequence SettingsDialog::validateShortcut(const QString& id, const QKeySequence& sequence)
{
    if (!sequence.isEmpty()) {
        for (auto it = m_currentKeys.constBegin(); it != m_currentKeys.constEnd(); ++it) {
            if (it.key() == id || it.value() != sequence)
                continue;
            // 冲突：提示是谁占用了，并把这一行还原成原来的按键
            if (m_shortcutWarning) {
                m_shortcutWarning->setText(
                    CSM_TR("%1 is already used by %2; pick another shortcut.")
                        .arg(sequence.toString(QKeySequence::NativeText), m_labels.value(it.key())));
                m_shortcutWarning->show();
            }
            return m_currentKeys.value(id);
        }
    }
    if (m_shortcutWarning)
        m_shortcutWarning->hide();
    m_currentKeys[id] = sequence;
    return sequence;
}

} // namespace csm
