#include "CategoryDialog.h"

#include "Widgets.h"
#include "core/Defaults.h"
#include "core/Store.h"
#include "core/Util.h"

#include <QButtonGroup>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>

CategoryDialog::CategoryDialog(QWidget *parent)
    : QDialog(parent)
{
    setMinimumWidth(460);
    auto *l = new QVBoxLayout(this);
    l->setSpacing(10);
    m_type = new SegmentedControl({{QStringLiteral("expense"), QStringLiteral("Expense")}, {QStringLiteral("income"), QStringLiteral("Income")}});
    l->addWidget(m_type, 0, Qt::AlignLeft);
    m_typeHint = ui::muted(QStringLiteral("Changing the type also changes the type of this category's existing transactions."));
    m_typeHint->hide();
    l->addWidget(m_typeHint);

    l->addWidget(new QLabel(QStringLiteral("<b>Name</b>")));
    m_name = new QLineEdit;
    m_name->setMaxLength(60);
    m_name->setObjectName(QStringLiteral("nameEdit"));
    m_name->setPlaceholderText(QStringLiteral("e.g. Cleaning Supplies"));
    l->addWidget(m_name);

    m_budgetRow = new QWidget;
    auto *bl = new QVBoxLayout(m_budgetRow);
    bl->setContentsMargins(0, 0, 0, 0);
    bl->addWidget(new QLabel(QStringLiteral("<b>Monthly budget</b> (optional)")));
    m_budget = new QLineEdit;
    m_budget->setPlaceholderText(QStringLiteral("No budget"));
    m_budget->setObjectName(QStringLiteral("budgetEdit"));
    bl->addWidget(m_budget);
    bl->addWidget(ui::muted(QStringLiteral("Shows progress on the dashboard and warns when spending goes over.")));
    l->addWidget(m_budgetRow);

    l->addWidget(new QLabel(QStringLiteral("<b>Icon</b>")));
    auto *grid = new QGridLayout;
    grid->setSpacing(4);
    m_icons = new QButtonGroup(this);
    m_icons->setExclusive(true);
    const auto &choices = defaults::iconChoices();
    for (int i = 0; i < choices.size(); ++i) {
        auto *b = new QToolButton;
        b->setText(choices[i]);
        b->setCheckable(true);
        b->setFixedSize(38, 34);
        QFont f = b->font();
        f.setPointSizeF(f.pointSizeF() * 1.3);
        b->setFont(f);
        m_icons->addButton(b, i);
        grid->addWidget(b, i / 10, i % 10);
    }
    l->addLayout(grid);

    auto *bb = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    bb->button(QDialogButtonBox::Save)->setObjectName(QStringLiteral("primary"));
    connect(bb, &QDialogButtonBox::accepted, this, &CategoryDialog::save);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    l->addWidget(bb);

    connect(m_type, &SegmentedControl::changed, this, &CategoryDialog::syncType);
}

void CategoryDialog::syncType()
{
    const TxType t = typeFromString(m_type->value());
    m_budgetRow->setVisible(t == TxType::Expense);
    m_typeHint->setVisible(m_editing && t != m_cat.type);
    adjustSize();
}

void CategoryDialog::setNew(TxType type)
{
    setWindowTitle(QStringLiteral("New category"));
    m_editing = false;
    m_cat = Category();
    m_cat.type = type;
    m_type->setValue(typeToString(type));
    const int idx = defaults::iconChoices().indexOf(QStringLiteral("🏷"));
    if (auto *b = m_icons->button(idx)) b->setChecked(true);
    syncType();
}

void CategoryDialog::setCategory(const Category &c)
{
    setWindowTitle(QStringLiteral("Edit category"));
    m_editing = true;
    m_cat = c;
    m_type->setValue(typeToString(c.type));
    m_name->setText(c.name);
    if (c.budget) m_budget->setText(util::amountPlain(c.budget));
    int idx = defaults::iconChoices().indexOf(c.icon);
    if (idx < 0) idx = defaults::iconChoices().indexOf(QStringLiteral("🏷"));
    if (auto *b = m_icons->button(idx)) b->setChecked(true);
    syncType();
}

void CategoryDialog::save()
{
    Category c = m_cat;
    c.name = m_name->text().simplified();
    c.type = typeFromString(m_type->value());
    c.budget = 0;
    if (c.type == TxType::Expense && !m_budget->text().trimmed().isEmpty()) {
        c.budget = util::parseAmount(m_budget->text());
        if (c.budget < 0) {
            QMessageBox::warning(this, windowTitle(), QStringLiteral("The budget must be a number."));
            m_budget->setFocus();
            return;
        }
    }
    if (m_icons->checkedButton()) c.icon = m_icons->checkedButton()->text();
    QString err;
    if (!Store::instance().saveCategory(c, &err)) {
        QMessageBox::warning(this, windowTitle(), err);
        m_name->setFocus();
        return;
    }
    m_cat.type = c.type;
    accept();
}
