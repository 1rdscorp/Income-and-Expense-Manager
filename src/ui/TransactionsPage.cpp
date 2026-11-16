#include "TransactionsPage.h"

#include "ReceiptViewer.h"
#include "Theme.h"
#include "Widgets.h"
#include "core/Backup.h"
#include "core/Store.h"
#include "core/Util.h"

#include <QComboBox>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QShortcut>
#include <QStandardPaths>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {
enum Col { ColDesc, ColCategory, ColVendor, ColPayment, ColReceipts, ColAmount, ColCount };
}

TransactionsPage::TransactionsPage(QWidget *parent)
    : QWidget(parent)
{
    const QDate today = QDate::currentDate();
    m_month = QDate(today.year(), today.month(), 1);
    m_year = today.year();

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(26, 20, 26, 20);
    root->setSpacing(14);

    auto *bar = new QHBoxLayout;
    bar->setSpacing(10);
    m_mode = new SegmentedControl({{QStringLiteral("month"), QStringLiteral("Month")}, {QStringLiteral("year"), QStringLiteral("Year")}, {QStringLiteral("all"), QStringLiteral("All")}});
    m_period = new PeriodNav;
    m_type = new QComboBox;
    m_type->addItem(QStringLiteral("Income & expenses"), QStringLiteral("all"));
    m_type->addItem(QStringLiteral("Income only"), QStringLiteral("income"));
    m_type->addItem(QStringLiteral("Expenses only"), QStringLiteral("expense"));
    m_category = new QComboBox;
    m_category->setMinimumWidth(190);
    m_category->setMaxVisibleItems(20);
    m_search = new QLineEdit;
    m_search->setPlaceholderText(QStringLiteral("Search description, vendor, notes, amount…"));
    m_search->setClearButtonEnabled(true);
    m_search->setMinimumWidth(220);
    m_search->addAction(Theme::icon(QStringLiteral("search")), QLineEdit::LeadingPosition);
    auto *csv = ui::button(QStringLiteral("Export CSV"), QStringLiteral("download"));
    csv->setToolTip(QStringLiteral("Export the filtered list as CSV"));
    bar->addWidget(m_mode);
    bar->addWidget(m_period);
    bar->addStretch(1);
    bar->addWidget(csv);
    root->addLayout(bar);
    auto *filters = new QHBoxLayout;
    filters->setSpacing(10);
    filters->addWidget(m_type);
    filters->addWidget(m_category);
    filters->addWidget(m_search, 1);
    root->addLayout(filters);

    auto *sum = new QHBoxLayout;
    sum->setSpacing(12);
    for (int i = 0; i < 4; ++i) {
        auto *k = new KpiCard;
        k->findChild<QLabel *>(QStringLiteral("kpiValue"))->setObjectName(QStringLiteral("kpiValueSmall"));
        m_summary << k;
        sum->addWidget(k);
    }
    root->addLayout(sum);

    auto *card = new Card;
    m_tree = new QTreeWidget;
    m_tree->setColumnCount(ColCount);
    m_tree->setHeaderLabels({QStringLiteral("Description"), QStringLiteral("Category"), QStringLiteral("Vendor / Payee"),
                             QStringLiteral("Payment"), QStringLiteral("Receipts"), QStringLiteral("Amount")});
    m_tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_tree->setUniformRowHeights(true);
    m_tree->setAlternatingRowColors(false);
    m_tree->setRootIsDecorated(false);
    m_tree->setItemsExpandable(false);
    m_tree->setExpandsOnDoubleClick(false);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    m_tree->header()->setStretchLastSection(false);
    m_tree->header()->setSectionResizeMode(ColDesc, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(ColCategory, QHeaderView::Interactive);
    m_tree->header()->setSectionResizeMode(ColVendor, QHeaderView::Interactive);
    m_tree->header()->setSectionResizeMode(ColPayment, QHeaderView::Interactive);
    m_tree->header()->setSectionResizeMode(ColReceipts, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(ColAmount, QHeaderView::Interactive);
    m_tree->setColumnWidth(ColCategory, 220);
    m_tree->setColumnWidth(ColVendor, 160);
    m_tree->setColumnWidth(ColPayment, 120);
    m_tree->setColumnWidth(ColAmount, 130);
    card->body()->addWidget(m_tree, 1);
    m_emptyHint = ui::muted(QString());
    m_emptyHint->setAlignment(Qt::AlignCenter);
    card->body()->addWidget(m_emptyHint);
    card->body()->addWidget(ui::muted(QStringLiteral("Double-click to edit · Right-click for more options · Del to delete selected")));
    root->addWidget(card, 1);

    m_searchTimer = new QTimer(this);
    m_searchTimer->setSingleShot(true);
    m_searchTimer->setInterval(180);

    connect(m_mode, &SegmentedControl::changed, this, [this]() { refresh(); });
    connect(m_period, &PeriodNav::stepped, this, [this](int d) {
        if (m_mode->value() == QLatin1String("month")) m_month = m_month.addMonths(d);
        else m_year += d;
        refresh();
    });
    connect(m_type, &QComboBox::currentIndexChanged, this, [this]() {
        rebuildCategoryCombo();
        updateList();
    });
    connect(m_category, &QComboBox::currentIndexChanged, this, [this]() { updateList(); });
    connect(m_search, &QLineEdit::textChanged, m_searchTimer, qOverload<>(&QTimer::start));
    connect(m_searchTimer, &QTimer::timeout, this, &TransactionsPage::updateList);
    connect(m_tree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *it) {
        const QString id = it->data(0, Qt::UserRole).toString();
        if (!id.isEmpty()) emit requestEdit(id);
    });
    connect(m_tree, &QTreeWidget::customContextMenuRequested, this, &TransactionsPage::showContextMenu);
    auto *del = new QShortcut(QKeySequence::Delete, m_tree);
    connect(del, &QShortcut::activated, this, &TransactionsPage::deleteSelected);
    connect(csv, &QPushButton::clicked, this, [this]() {
        const auto list = filtered();
        if (list.isEmpty()) {
            QMessageBox::information(this, QStringLiteral("Export CSV"), QStringLiteral("There are no transactions to export."));
            return;
        }
        const QString def = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + QStringLiteral("/transactions.csv");
        const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Export transactions"), def, QStringLiteral("CSV files (*.csv)"));
        if (path.isEmpty()) return;
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly) || f.write(backup::transactionsCsv(Store::instance(), list).toUtf8()) < 0)
            QMessageBox::warning(this, QStringLiteral("Export failed"), f.errorString());
    });
}

void TransactionsPage::showMonth(int year, int month)
{
    m_mode->setValue(QStringLiteral("month"));
    m_month = QDate(year, month, 1);
    m_type->setCurrentIndex(0);
    m_search->clear();
    m_category->setCurrentIndex(0);
    refresh();
}

void TransactionsPage::rebuildCategoryCombo()
{
    const QString current = m_category->currentData().toString();
    const QString type = m_type->currentData().toString();
    QSignalBlocker block(m_category);
    m_category->clear();
    m_category->addItem(QStringLiteral("All categories"), QStringLiteral("all"));
    for (TxType tt : {TxType::Income, TxType::Expense}) {
        if (type != QLatin1String("all") && typeFromString(type) != tt) continue;
        if (type == QLatin1String("all")) {
            m_category->addItem(tt == TxType::Income ? QStringLiteral("— Income —") : QStringLiteral("— Expenses —"), QStringLiteral("-"));
            m_category->setItemData(m_category->count() - 1, 0, Qt::UserRole - 1); // disable header row
        }
        for (const auto &c : Store::instance().categoriesOfType(tt)) m_category->addItem(c.icon + QStringLiteral("  ") + c.name, c.id);
    }
    const int idx = (current.isEmpty() || current == QLatin1String("-")) ? -1 : m_category->findData(current);
    m_category->setCurrentIndex(idx >= 0 ? idx : 0);
}

void TransactionsPage::refresh()
{
    const QString mode = m_mode->value();
    m_period->setVisible(mode != QLatin1String("all"));
    m_period->setLabel(mode == QLatin1String("month") ? util::monthLabel(m_month.year(), m_month.month()) : QString::number(m_year));
    rebuildCategoryCombo();
    updateList();
}

QVector<Transaction> TransactionsPage::filtered() const
{
    const QString mode = m_mode->value();
    const QString type = m_type->currentData().toString();
    const QString cat = m_category->currentData().toString();
    const QString q = m_search->text().trimmed().toLower();
    const Store &s = Store::instance();
    return s.filtered([&](const Transaction &t) {
        if (mode == QLatin1String("month") && (t.date.year() != m_month.year() || t.date.month() != m_month.month())) return false;
        if (mode == QLatin1String("year") && t.date.year() != m_year) return false;
        if (type != QLatin1String("all") && typeToString(t.type) != type) return false;
        if (!cat.isEmpty() && cat != QLatin1String("all") && cat != QLatin1String("-") && t.categoryId != cat) return false;
        if (!q.isEmpty()) {
            const QString hay = (t.description + QLatin1Char(' ') + t.vendor + QLatin1Char(' ') + t.notes + QLatin1Char(' ') + t.paymentMethod +
                                 QLatin1Char(' ') + s.categoryOrUnknown(t.categoryId).name + QLatin1Char(' ') + util::amountPlain(t.amount))
                                    .toLower();
            if (!hay.contains(q)) return false;
        }
        return true;
    });
}

void TransactionsPage::updateList()
{
    const Store &s = Store::instance();
    const auto &th = Theme::c();
    const auto list = filtered();
    const Totals t = Store::totals(list);
    m_summary[0]->set(QStringLiteral("Income"), util::money(t.income), QString(), th.income);
    m_summary[1]->set(QStringLiteral("Expenses"), util::money(t.expense), QString(), th.expense);
    m_summary[2]->set(QStringLiteral("Net"), util::signedMoney(t.net()), QString(), QColor(), t.net() < 0);
    m_summary[3]->set(QStringLiteral("Transactions"), QString::number(t.count));

    m_tree->setUpdatesEnabled(false);
    m_tree->clear();
    QFont bold = m_tree->font();
    bold.setWeight(QFont::DemiBold);
    QTreeWidgetItem *group = nullptr;
    QDate groupDate;
    Totals dayTotals;
    auto finishGroup = [&]() {
        if (!group) return;
        QStringList parts;
        if (dayTotals.income) parts << QStringLiteral("+%1").arg(util::money(dayTotals.income));
        if (dayTotals.expense) parts << QStringLiteral("Spent %1").arg(util::money(dayTotals.expense));
        group->setText(ColDesc, group->text(ColDesc) + QStringLiteral("      ") + parts.join(QStringLiteral("  ·  ")));
        group->setFirstColumnSpanned(true);
    };
    for (const auto &x : list) {
        if (x.date != groupDate) {
            finishGroup();
            groupDate = x.date;
            dayTotals = Totals();
            group = new QTreeWidgetItem(m_tree);
            group->setText(ColDesc, util::fmtDay(x.date).toUpper());
            group->setFlags(Qt::ItemIsEnabled);
            for (int c = 0; c < ColCount; ++c) {
                group->setBackground(c, th.surface3);
                group->setFont(c, bold);
                group->setForeground(c, th.text2);
            }
        }
        if (x.type == TxType::Income) dayTotals.income += x.amount;
        else dayTotals.expense += x.amount;
        const Category c = s.categoryOrUnknown(x.categoryId);
        auto *it = new QTreeWidgetItem(group);
        it->setText(ColDesc, x.description.isEmpty() ? (x.vendor.isEmpty() ? c.name : x.vendor) : x.description);
        if (!x.notes.isEmpty()) it->setToolTip(ColDesc, x.notes);
        it->setText(ColCategory, c.icon + QStringLiteral("  ") + c.name);
        it->setText(ColVendor, x.vendor);
        it->setText(ColPayment, x.paymentMethod);
        const int rc = s.receiptCount(x.id);
        it->setText(ColReceipts, rc ? QStringLiteral("📎 %1").arg(rc) : QString());
        it->setTextAlignment(ColReceipts, Qt::AlignCenter);
        it->setText(ColAmount, (x.type == TxType::Income ? QStringLiteral("+") : QStringLiteral("−")) + util::money(x.amount));
        it->setTextAlignment(ColAmount, Qt::AlignRight | Qt::AlignVCenter);
        it->setFont(ColAmount, bold);
        it->setForeground(ColAmount, x.type == TxType::Income ? th.goodText : th.text);
        it->setForeground(ColVendor, th.text2);
        it->setForeground(ColPayment, th.text2);
        it->setData(0, Qt::UserRole, x.id);
    }
    finishGroup();
    m_tree->expandAll();
    m_tree->setUpdatesEnabled(true);

    m_emptyHint->setVisible(list.isEmpty());
    m_emptyHint->setText(s.transactions().isEmpty() ? QStringLiteral("No transactions yet. Click “Add transaction” to record your first one.")
                                                    : QStringLiteral("No transactions match these filters."));
}

QStringList TransactionsPage::selectedIds() const
{
    QStringList ids;
    for (auto *it : m_tree->selectedItems()) {
        const QString id = it->data(0, Qt::UserRole).toString();
        if (!id.isEmpty()) ids << id;
    }
    return ids;
}

void TransactionsPage::deleteSelected()
{
    const QStringList ids = selectedIds();
    if (ids.isEmpty()) return;
    qint64 total = 0;
    int receipts = 0;
    for (const auto &id : ids)
        if (const Transaction *t = Store::instance().transaction(id)) {
            total += t->amount;
            receipts += Store::instance().receiptCount(id);
        }
    const QString msg = ids.size() == 1
                            ? QStringLiteral("Delete this transaction of %1%2?").arg(util::money(total), receipts ? QStringLiteral(" and its %1 receipt(s)").arg(receipts) : QString())
                            : QStringLiteral("Delete %1 transactions (total %2)%3?").arg(ids.size()).arg(util::money(total), receipts ? QStringLiteral(" and %1 receipt(s)").arg(receipts) : QString());
    if (QMessageBox::question(this, QStringLiteral("Delete"), msg + QStringLiteral("\n\nThis can't be undone.")) != QMessageBox::Yes) return;
    QString err;
    if (!Store::instance().deleteTransactions(ids, &err)) QMessageBox::warning(this, QStringLiteral("Delete failed"), err);
}

void TransactionsPage::showContextMenu(const QPoint &pos)
{
    QTreeWidgetItem *item = m_tree->itemAt(pos);
    const QString id = item ? item->data(0, Qt::UserRole).toString() : QString();
    QMenu menu(this);
    if (!id.isEmpty()) {
        if (!item->isSelected()) {
            m_tree->clearSelection();
            item->setSelected(true);
        }
        menu.addAction(Theme::icon(QStringLiteral("edit")), QStringLiteral("Edit…"), this, [this, id]() { emit requestEdit(id); });
        menu.addAction(Theme::icon(QStringLiteral("plus")), QStringLiteral("Duplicate (for today)…"), this, [this, id]() { emit requestDuplicate(id); });
        const auto receipts = Store::instance().receiptCount(id);
        if (receipts) {
            QMenu *rm = menu.addMenu(Theme::icon(QStringLiteral("clip")), QStringLiteral("View receipt"));
            for (const auto &r : Store::instance().receiptsFor(id))
                rm->addAction(r.name, this, [this, r]() { ReceiptViewer::display(this, r); });
        }
        menu.addSeparator();
        const int n = selectedIds().size();
        menu.addAction(Theme::icon(QStringLiteral("trash")), n > 1 ? QStringLiteral("Delete %1 transactions").arg(n) : QStringLiteral("Delete"), this,
                       &TransactionsPage::deleteSelected);
    } else {
        menu.addAction(Theme::icon(QStringLiteral("plus")), QStringLiteral("Add transaction…"), this, &TransactionsPage::requestAdd);
    }
    menu.exec(m_tree->viewport()->mapToGlobal(pos));
}
