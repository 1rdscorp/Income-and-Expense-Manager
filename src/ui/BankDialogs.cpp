#include "BankDialogs.h"

#include "Widgets.h"
#include "core/BankImport.h"
#include "core/Store.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QStandardPaths>
#include <QVBoxLayout>

void fillCategoryCombo(QComboBox *combo, const QString &selected, bool onlyType, TxType type)
{
    combo->clear();
    for (TxType t : {TxType::Expense, TxType::Income}) {
        if (onlyType && t != type) continue;
        if (!onlyType) {
            combo->addItem(t == TxType::Expense ? QStringLiteral("— Expenses —") : QStringLiteral("— Income —"), QString());
            combo->setItemData(combo->count() - 1, 0, Qt::UserRole - 1);
        }
        for (const auto &c : Store::instance().categoriesOfType(t)) combo->addItem(c.icon + QStringLiteral("  ") + c.name, c.id);
    }
    const int idx = combo->findData(selected);
    if (idx >= 0) combo->setCurrentIndex(idx);
    else
        for (int i = 0; i < combo->count(); ++i)
            if (!combo->itemData(i).toString().isEmpty()) {
                combo->setCurrentIndex(i);
                break;
            }
}

/* ---------------- AccountDialog ---------------- */

AccountDialog::AccountDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("New bank account or card"));
    setMinimumWidth(520);
    auto *l = new QVBoxLayout(this);
    l->setSpacing(8);
    m_kind = new SegmentedControl({{QStringLiteral("bank"), QStringLiteral("  Bank account  ")}, {QStringLiteral("card"), QStringLiteral("  Credit card  ")}});
    l->addWidget(m_kind, 0, Qt::AlignLeft);
    l->addWidget(new QLabel(QStringLiteral("<b>Name</b>")));
    m_name = new QLineEdit;
    m_name->setObjectName(QStringLiteral("accountName"));
    m_name->setMaxLength(80);
    m_name->setPlaceholderText(QStringLiteral("e.g. Business Checking, Amex Gold ••1004"));
    l->addWidget(m_name);
    l->addWidget(ui::muted(QStringLiteral("Shown as the payment method on imported transactions.")));
    m_flip = new QCheckBox(QStringLiteral("Statements show money going out as positive numbers"));
    l->addSpacing(4);
    l->addWidget(m_flip);
    l->addWidget(ui::muted(QStringLiteral("Some credit-card CSV files list charges as positive amounts. Tick this if imported purchases show up as income.")));

    l->addSpacing(6);
    l->addWidget(new QLabel(QStringLiteral("<b>Automatic import folder</b> (optional)")));
    auto *row = new QHBoxLayout;
    m_folder = new QLineEdit;
    m_folder->setReadOnly(true);
    m_folder->setPlaceholderText(QStringLiteral("Not set"));
    auto *choose = ui::button(QStringLiteral("Choose…"), QStringLiteral("folder"));
    auto *clear = ui::button(QStringLiteral("Clear"));
    row->addWidget(m_folder, 1);
    row->addWidget(choose);
    row->addWidget(clear);
    l->addLayout(row);
    l->addWidget(ui::muted(QStringLiteral("Statement files (OFX, QFX, QIF or CSV) saved into this folder are imported automatically while "
                                          "Ledgerly is open, then moved to an “Imported” subfolder. Tip: set your bank's downloads or "
                                          "e-statement exports to save here.")));

    auto *bb = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    bb->button(QDialogButtonBox::Save)->setObjectName(QStringLiteral("primary"));
    l->addWidget(bb);
    connect(bb, &QDialogButtonBox::accepted, this, &AccountDialog::save);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(choose, &QPushButton::clicked, this, [this]() {
        const QString start = m_folder->text().isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::DownloadLocation) : m_folder->text();
        const QString d = QFileDialog::getExistingDirectory(this, QStringLiteral("Folder to import statements from"), start);
        if (!d.isEmpty()) m_folder->setText(QDir::toNativeSeparators(d));
    });
    connect(clear, &QPushButton::clicked, m_folder, &QLineEdit::clear);
}

void AccountDialog::setAccount(const Account &a)
{
    setWindowTitle(QStringLiteral("Edit %1").arg(a.isCard() ? QStringLiteral("credit card") : QStringLiteral("bank account")));
    m_account = a;
    m_name->setText(a.name);
    m_kind->setValue(a.kind);
    m_flip->setChecked(a.flipSigns);
    m_folder->setText(QDir::toNativeSeparators(bank::watchFolder(Store::instance(), a.id)));
}

void AccountDialog::save()
{
    Account a = m_account;
    a.name = m_name->text();
    a.kind = m_kind->value();
    a.flipSigns = m_flip->isChecked();
    QString err;
    if (!Store::instance().saveAccount(a, &err, &m_savedId)) {
        QMessageBox::warning(this, windowTitle(), err);
        return;
    }
    bank::setWatchFolder(Store::instance(), m_savedId, QDir::fromNativeSeparators(m_folder->text()));
    accept();
}

/* ---------------- RuleDialog ---------------- */

RuleDialog::RuleDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("New import rule"));
    setMinimumWidth(480);
    auto *l = new QVBoxLayout(this);
    l->setSpacing(8);
    l->addWidget(new QLabel(QStringLiteral("<b>When the bank description contains</b>")));
    m_pattern = new QLineEdit;
    m_pattern->setObjectName(QStringLiteral("rulePattern"));
    m_pattern->setPlaceholderText(QStringLiteral("e.g. AMAZON, SHELL, PAYROLL"));
    l->addWidget(m_pattern);
    l->addWidget(ui::muted(QStringLiteral("Not case-sensitive. The longest matching rule wins.")));
    l->addSpacing(4);
    m_useCategory = new QRadioButton(QStringLiteral("Put it in this category:"));
    m_category = new QComboBox;
    m_category->setMaxVisibleItems(20);
    m_ignore = new QRadioButton(QStringLiteral("Don't import it (transfers between your own accounts, credit-card payments)"));
    m_useCategory->setChecked(true);
    l->addWidget(m_useCategory);
    l->addWidget(m_category);
    l->addWidget(m_ignore);
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    bb->button(QDialogButtonBox::Save)->setObjectName(QStringLiteral("primary"));
    l->addWidget(bb);
    fillCategoryCombo(m_category);
    connect(m_useCategory, &QRadioButton::toggled, m_category, &QWidget::setEnabled);
    connect(bb, &QDialogButtonBox::accepted, this, &RuleDialog::save);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void RuleDialog::setRule(const ImportRule &r)
{
    setWindowTitle(QStringLiteral("Edit import rule"));
    m_rule = r;
    m_pattern->setText(r.pattern);
    m_ignore->setChecked(r.ignore);
    m_useCategory->setChecked(!r.ignore);
    fillCategoryCombo(m_category, r.categoryId);
}

void RuleDialog::setSuggestion(const QString &pattern, const QString &categoryId)
{
    m_pattern->setText(pattern);
    fillCategoryCombo(m_category, categoryId);
}

void RuleDialog::save()
{
    ImportRule r = m_rule;
    r.pattern = m_pattern->text();
    r.ignore = m_ignore->isChecked();
    r.categoryId = m_category->currentData().toString();
    QString err;
    if (!Store::instance().saveImportRule(r, &err)) {
        QMessageBox::warning(this, windowTitle(), err);
        return;
    }
    accept();
}
