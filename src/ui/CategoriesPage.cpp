#include "CategoriesPage.h"

#include "CategoryDialog.h"
#include "Theme.h"
#include "Widgets.h"
#include "core/Store.h"
#include "core/Util.h"

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QRadioButton>
#include <QShortcut>
#include <QTableWidget>
#include <QVBoxLayout>
#include <algorithm>

CategoriesPage::CategoriesPage(QWidget *parent)
    : QWidget(parent)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(26, 20, 26, 20);
    root->setSpacing(14);

    auto *bar = new QHBoxLayout;
    m_tab = new SegmentedControl({{QStringLiteral("expense"), QStringLiteral("Expense categories")}, {QStringLiteral("income"), QStringLiteral("Income categories")}});
    auto *add = ui::button(QStringLiteral("Add category"), QStringLiteral("plus"), QStringLiteral("primary"));
    auto *edit = ui::button(QStringLiteral("Edit"), QStringLiteral("edit"));
    auto *del = ui::button(QStringLiteral("Delete"), QStringLiteral("trash"), QStringLiteral("dangerText"));
    bar->addWidget(m_tab);
    bar->addStretch(1);
    bar->addWidget(edit);
    bar->addWidget(del);
    bar->addWidget(add);
    root->addLayout(bar);

    auto *card = new Card;
    m_table = new QTableWidget;
    m_table->setColumnCount(5);
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setShowGrid(false);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int c = 1; c < 5; ++c) m_table->horizontalHeader()->setSectionResizeMode(c, QHeaderView::ResizeToContents);
    m_table->verticalHeader()->setDefaultSectionSize(40);
    card->body()->addWidget(m_table, 1);
    card->body()->addWidget(ui::muted(QStringLiteral("Double-click a category to edit it. Monthly budgets show progress on the dashboard and warn when spending goes over.")));
    root->addWidget(card, 1);

    connect(m_tab, &SegmentedControl::changed, this, &CategoriesPage::refresh);
    connect(add, &QPushButton::clicked, this, &CategoriesPage::addCategory);
    connect(edit, &QPushButton::clicked, this, [this]() { editCategory(selectedId()); });
    connect(del, &QPushButton::clicked, this, [this]() { deleteCategory(selectedId()); });
    connect(m_table, &QTableWidget::cellDoubleClicked, this, [this](int row) { editCategory(m_table->item(row, 0)->data(Qt::UserRole).toString()); });
    connect(new QShortcut(QKeySequence::Delete, m_table), &QShortcut::activated, this, [this]() { deleteCategory(selectedId()); });
}

QString CategoriesPage::selectedId() const
{
    const int row = m_table->currentRow();
    if (row < 0 || !m_table->item(row, 0)) return {};
    return m_table->item(row, 0)->data(Qt::UserRole).toString();
}

void CategoriesPage::refresh()
{
    const Store &s = Store::instance();
    const auto &th = Theme::c();
    const TxType type = typeFromString(m_tab->value());
    m_tab->setLabel(QStringLiteral("expense"), QStringLiteral("Expense categories (%1)").arg(s.categoriesOfType(TxType::Expense).size()));
    m_tab->setLabel(QStringLiteral("income"), QStringLiteral("Income categories (%1)").arg(s.categoriesOfType(TxType::Income).size()));

    const QDate today = QDate::currentDate();
    const auto monthBy = Store::byCategory(s.inMonth(today.year(), today.month()));
    const auto yearBy = Store::byCategory(s.inYear(today.year()));
    const auto allBy = Store::byCategory(s.transactions());
    const auto list = s.categoriesOfType(type);
    const QString prevSel = selectedId();

    m_table->setHorizontalHeaderLabels({QStringLiteral("Name"), type == TxType::Expense ? QStringLiteral("Monthly budget") : QString(),
                                        QStringLiteral("Transactions"), util::monthLabel(today.year(), today.month(), true),
                                        QStringLiteral("%1 total").arg(today.year())});
    m_table->setColumnHidden(1, type == TxType::Income);
    for (int c = 0; c < 5; ++c)
        if (auto *h = m_table->horizontalHeaderItem(c)) h->setTextAlignment((c == 0 ? Qt::AlignLeft : Qt::AlignRight) | Qt::AlignVCenter);
    m_table->setRowCount(list.size());
    auto cell = [&](int r, int c, const QString &text, bool right = true) {
        auto *it = new QTableWidgetItem(text);
        if (right) it->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_table->setItem(r, c, it);
        return it;
    };
    int selRow = -1;
    for (int r = 0; r < list.size(); ++r) {
        const Category &c = list[r];
        auto *name = cell(r, 0, c.icon + QStringLiteral("   ") + c.name, false);
        name->setData(Qt::UserRole, c.id);
        if (c.id == prevSel) selRow = r;
        cell(r, 1, c.budget ? util::money(c.budget) : QStringLiteral("—"))->setForeground(c.budget ? th.text : th.muted);
        const auto a = allBy.value(c.id);
        cell(r, 2, a.count ? QString::number(a.count) : QStringLiteral("Not used yet"))->setForeground(a.count ? th.text : th.muted);
        const auto m = monthBy.value(c.id);
        auto *mi = cell(r, 3, m.count ? util::money(m.total) : QStringLiteral("—"));
        if (c.budget && m.total > c.budget) {
            mi->setText(QStringLiteral("⚠ ") + util::money(m.total));
            mi->setForeground(th.badText);
            mi->setToolTip(QStringLiteral("Over budget by %1").arg(util::money(m.total - c.budget)));
        } else if (!m.count) {
            mi->setForeground(th.muted);
        }
        const auto y = yearBy.value(c.id);
        cell(r, 4, y.count ? util::money(y.total) : QStringLiteral("—"))->setForeground(y.count ? th.text : th.muted);
    }
    if (selRow >= 0) m_table->selectRow(selRow);
}

void CategoriesPage::addCategory()
{
    CategoryDialog dlg(this);
    dlg.setNew(typeFromString(m_tab->value()));
    if (dlg.exec() == QDialog::Accepted) {
        m_tab->setValue(typeToString(dlg.savedType()));
        refresh();
    }
}

void CategoriesPage::editCategory(const QString &id)
{
    const Category *c = Store::instance().category(id);
    if (!c) {
        QMessageBox::information(this, QStringLiteral("Edit category"), QStringLiteral("Select a category first."));
        return;
    }
    CategoryDialog dlg(this);
    dlg.setCategory(*c);
    if (dlg.exec() == QDialog::Accepted) {
        m_tab->setValue(typeToString(dlg.savedType()));
        refresh();
    }
}

void CategoriesPage::deleteCategory(const QString &id)
{
    Store &s = Store::instance();
    const Category *cp = s.category(id);
    if (!cp) {
        QMessageBox::information(this, QStringLiteral("Delete category"), QStringLiteral("Select a category first."));
        return;
    }
    const Category c = *cp;
    int used = 0;
    qint64 total = 0;
    for (const auto &t : s.transactions())
        if (t.categoryId == id) {
            ++used;
            total += t.amount;
        }
    QString err;
    if (!used) {
        if (QMessageBox::question(this, QStringLiteral("Delete category"), QStringLiteral("Delete the category “%1”?").arg(c.name)) != QMessageBox::Yes) return;
        if (!s.deleteCategory(id, QString(), &err)) QMessageBox::warning(this, QStringLiteral("Delete failed"), err);
        return;
    }

    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("Delete “%1”").arg(c.name));
    auto *l = new QVBoxLayout(&dlg);
    auto *msg = new QLabel(QStringLiteral("<b>%1</b> transaction%2 (%3) use this category. What should happen to %4?")
                               .arg(used).arg(used > 1 ? QStringLiteral("s") : QString(), util::money(total), used > 1 ? QStringLiteral("them") : QStringLiteral("it")));
    msg->setWordWrap(true);
    l->addWidget(msg);
    QVector<Category> others;
    for (const auto &o : s.categoriesOfType(c.type))
        if (o.id != id) others << o;
    auto *move = new QRadioButton(QStringLiteral("Move them to another category:"));
    auto *combo = new QComboBox;
    for (const auto &o : others) combo->addItem(o.icon + QStringLiteral("  ") + o.name, o.id);
    auto *remove = new QRadioButton(QStringLiteral("Delete the transactions and their receipts"));
    if (others.isEmpty()) {
        move->setEnabled(false);
        combo->setEnabled(false);
        remove->setChecked(true);
    } else {
        move->setChecked(true);
    }
    connect(move, &QRadioButton::toggled, combo, &QWidget::setEnabled);
    l->addWidget(move);
    l->addWidget(combo);
    l->addWidget(remove);
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Cancel);
    auto *go = bb->addButton(QStringLiteral("Delete category"), QDialogButtonBox::AcceptRole);
    go->setObjectName(QStringLiteral("danger"));
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    l->addWidget(bb);
    dlg.setMinimumWidth(440);
    if (dlg.exec() != QDialog::Accepted) return;
    const QString moveTo = move->isChecked() ? combo->currentData().toString() : QString();
    if (!s.deleteCategory(id, moveTo, &err)) QMessageBox::warning(this, QStringLiteral("Delete failed"), err);
}
