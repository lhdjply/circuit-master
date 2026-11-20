#include "ui/SettingsDialog.h"

#include "core/Grid.h"
#include <QtGlobal>
#include "core/Language.h"
#include "ui/Dialogs.h"
#include "ui/Theme.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace csm {

namespace {

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
                               std::function<void()> applyGrid)
    : QDialog(parent)
    , m_originalTheme(int(Theme::mode()))
    , m_applyTheme(std::move(applyTheme))
    , m_applyGrid(std::move(applyGrid))
{
    setWindowTitle(CSM_TR("Settings"));
    setModal(true);
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    Dialogs::applyStyle(*this);   // 和其它提示框同一套观感

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(20, 18, 20, 14);
    outer->setSpacing(14);

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

    outer->addLayout(form);

    auto* hint = new QLabel(CSM_TR("The interface language applies after a restart."), this);
    hint->setWordWrap(true);
    hint->setStyleSheet(QStringLiteral("color: %1; font-size: 11px;").arg(Theme::textMuted().name()));
    outer->addWidget(hint);
    outer->addStretch(1);

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

    connect(okButton, &QPushButton::clicked, this, [this, themeBox, gridBox, languageBox] {
        const double grid = gridBox->currentData().toDouble();
        if (!qFuzzyCompare(grid, Grid::size())) {
            Grid::setSize(grid);
            if (m_applyGrid)
                m_applyGrid();
        }

        const Theme::Mode theme = Theme::Mode(themeBox->currentData().toInt());
        if (int(theme) != m_originalTheme) {
            Theme::save(theme);
            if (m_applyTheme)
                m_applyTheme();
        }

        const Language chosen = Language(languageBox->currentData().toInt());
        const bool changed = chosen != language();
        saveLanguage(chosen);
        accept();
        if (changed)
            Dialogs::info(parentWidget(), CSM_TR("Settings"),
                          CSM_TR("The interface language has been changed. "
                                 "Restart the application to apply it."));
    });
    connect(cancelButton, &QPushButton::clicked, this, &QDialog::reject);
    outer->addLayout(buttonRow);
}

} // namespace csm
