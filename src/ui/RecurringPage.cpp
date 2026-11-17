#include "RecurringPage.h"

#include "RecurringDialog.h"
#include "Theme.h"
#include "Widgets.h"
#include "core/Recurrence.h"
#include "core/Store.h"
#include "core/Util.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QShortcut>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {
enum Col { ColName, ColCategory, ColAmount, ColSchedule, ColNext, ColRecorded, ColCount };

QString dueText(const QDate &d)
{
    const qint64 days = QDate::currentDate().daysTo(d);
    if (days == 0) return QStringLiteral("today");
    if (days == 1) return QStringLiteral("tomorrow");
    if (days < 0) return QStringLiteral("overdue");
    return QStringLiteral("in %1 days").arg(days);
}
} // namespace

RecurringPage::RecurringPage(QWidget *parent)
    : QWidget(parent)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(26, 20, 26, 20);
    root->setSpacing(14);

    auto *bar = new QHBoxLayout;
    auto *intro = ui::muted(QStringLiteral("Regular bills and income — rent, internet, salaries, subscriptions. "
                                           "Each payment is recorded automatically on its due date."));
    intro->setStyleSheet(QStringLiteral("font-size:13px;"));
    bar->addWidget(intro, 1);
    m_edit = ui::button(QStringLiteral("Edit"), QStringLiteral("edit"));
    m_pause = ui::button(QStringLiteral("Pause"));
    m_delete = ui::button(QStringLiteral("Delete"), QStringLiteral("trash"), QStringLiteral("dangerText"));
    auto *add = ui::button(QStringLiteral("Add recurring"), QStringLiteral("plus"), QStringLiteral("primary"));
    bar->addWidget(m_edit);
    bar->addWidget(m_pause);
    bar->addWidget(m_delete);
    bar->addWidget(add);
    root->addLayout(bar);

    auto *kpiRow = new QHBoxLayout;
    kpiRow->setSpacing(12);
    for (int i = 0; i < 3; ++i) {
        auto *k = new KpiCard;
        m_kpis << k;
        kpiRow->addWidget(k);
    }
    root->addLayout(kpiRow);

    auto *card = new Card;
    m_table = new QTableWidget(0, ColCount);
    m_table->setHorizontalHeaderLabels({QStringLiteral("Description"), QStringLiteral("Category"), QStringLiteral("Amount"),
                                        QStringLiteral("Schedule"), QStringLiteral("Next payment"), QStringLiteral("Recorded")});
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setShowGrid(false);
    m_table->verticalHeader()->setDefaultSectionSize(40);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(ColName, QHeaderView::Stretch);
    for (int c : {ColAmount, ColRecorded})
        m_table->horizontalHeaderItem(c)->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    for (int c : {ColName, ColCategory, ColSchedule, ColNext})
        m_table->horizontalHeaderItem(c)->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    card->body()->addWidget(m_table, 1);

    m_empty = new QWidget;
    auto *el = new QVBoxLayout(m_empty);
    el->setContentsMargins(0, 30, 0, 30);
    auto *emptyIcon = new QLabel;
    emptyIcon->setPixmap(Theme::iconPixmap(QStringLiteral("repeat"), Theme::c().muted, 40));
    emptyIcon->setAlignment(Qt::AlignCenter);
    auto *emptyTitle = new QLabel(QStringLiteral("No recurring transactions yet"));
    emptyTitle->setAlignment(Qt::AlignCenter);
    emptyTitle->setStyleSheet(QStringLiteral("font-size:16px;font-weight:600;"));
    auto *emptyText = ui::muted(QStringLiteral("Add rent, internet, insurance, salaries or subscriptions once — Ledgerly records them for you every period."));
    emptyText->setAlignment(Qt::AlignCenter);
    auto *emptyAdd = ui::button(QStringLiteral("Add your first recurring transaction"), QStringLiteral("plus"), QStringLiteral("primary"));
    el->addWidget(emptyIcon);
    el->addWidget(emptyTitle);
    el->addWidget(emptyText);
    el->addSpacing(8);
    el->addWidget(emptyAdd, 0, Qt::AlignCenter);
    card->body()->addWidget(m_empty);
    card->body()->addWidget(ui::muted(QStringLiteral("Payments are recorded when their date arrives while Ledgerly is open; any that fell due while it was "
                                                     "closed are added the next time you open it. Edit a recorded payment on the Transactions page if the "
                                                     "amount differs (e.g. a utility bill). Double-click a row to edit.")));
    root->addWidget(card, 1);

    connect(add, &QPushButton::clicked, this, &RecurringPage::addRule);
    connect(emptyAdd, &QPushButton::clicked, this, &RecurringPage::addRule);
    connect(m_edit, &QPushButton::clicked, this, [this]() { editRule(selectedId()); });
    connect(m_pause, &QPushButton::clicked, this, [this]() { togglePause(selectedId()); });
    connect(m_delete, &QPushButton::clicked, this, [this]() { deleteRule(selectedId()); });
    connect(m_table, &QTableWidget::cellDoubleClicked, this, [this](int row) { editRule(m_table->item(row, 0)->data(Qt::UserRole).toString()); });
    connect(m_table, &QTableWidget::itemSelectionChanged, this, &RecurringPage::updateButtons);
    connect(new QShortcut(QKeySequence::Delete, m_table), &QShortcut::activated, this, [this]() { deleteRule(selectedId()); });
}

QString RecurringPage::selectedId() const
{
    const int row = m_table->currentRow();
    if (row < 0 || !m_table->item(row, 0) || m_table->selectedItems().isEmpty()) return {};
    return m_table->item(row, 0)->data(Qt::UserRole).toString();
}

void RecurringPage::updateButtons()
{
    const Recurring *r = Store::instance().recurringRule(selectedId());
    m_edit->setEnabled(r);
    m_delete->setEnabled(r);
    m_pause->setEnabled(r && !r->finished());
    m_pause->setText(r && !r->active ? QStringLiteral("Resume") : QStringLiteral("Pause"));
}

void RecurringPage::refresh()
{
    const Store &s = Store::instance();
    const auto &th = Theme::c();
    const QDate today = QDate::currentDate();
    const auto &rules = s.recurring();

    qint64 monthlyExp = 0, monthlyInc = 0;
    int activeExp = 0, activeInc = 0;
    for (const auto &r : rules) {
        if (!r.active || r.finished()) continue;
        if (r.type == TxType::Expense) {
            monthlyExp += recurrence::monthlyEquivalent(r);
            ++activeExp;
        } else {
            monthlyInc += recurrence::monthlyEquivalent(r);
            ++activeInc;
        }
    }
    qint64 dueExp = 0;
    int dueCount = 0;
    for (const auto &u : s.upcoming(today, today.addDays(30)))
        if (u.second.type == TxType::Expense) {
            dueExp += u.second.amount;
            ++dueCount;
        }
    m_kpis[0]->set(QStringLiteral("Fixed costs per month"), util::money(monthlyExp),
                   QStringLiteral("%1 active recurring expense%2").arg(activeExp).arg(activeExp == 1 ? QString() : QStringLiteral("s")), th.expense);
    m_kpis[1]->set(QStringLiteral("Recurring income per month"), util::money(monthlyInc),
                   QStringLiteral("%1 active recurring income item%2").arg(activeInc).arg(activeInc == 1 ? QString() : QStringLiteral("s")), th.income);
    m_kpis[2]->set(QStringLiteral("Bills due in the next 30 days"), util::money(dueExp),
                   QStringLiteral("%1 payment%2").arg(dueCount).arg(dueCount == 1 ? QString() : QStringLiteral("s")));

    const QString prev = selectedId();
    m_table->setRowCount(rules.size());
    m_table->setVisible(!rules.isEmpty());
    m_empty->setVisible(rules.isEmpty());
    QFont bold = m_table->font();
    bold.setWeight(QFont::DemiBold);
    int sel = -1;
    for (int i = 0; i < rules.size(); ++i) {
        const Recurring &r = rules[i];
        const Category c = s.categoryOrUnknown(r.categoryId);
        const bool dim = !r.active || r.finished();
        auto set = [&](int col, const QString &text, bool right = false) {
            auto *it = new QTableWidgetItem(text);
            it->setTextAlignment((right ? Qt::AlignRight : Qt::AlignLeft) | Qt::AlignVCenter);
            if (dim) it->setForeground(th.muted);
            m_table->setItem(i, col, it);
            return it;
        };
        auto *name = set(ColName, r.title().isEmpty() ? c.name : r.title());
        name->setData(Qt::UserRole, r.id);
        name->setIcon(QIcon(Theme::iconPixmap(QStringLiteral("repeat"), dim ? th.muted : th.accent, 16)));
        if (!r.vendor.isEmpty() && !r.description.isEmpty()) name->setToolTip(r.vendor);
        set(ColCategory, c.icon + QStringLiteral("  ") + c.name);
        auto *amt = set(ColAmount, (r.type == TxType::Income ? QStringLiteral("+") : QStringLiteral("−")) + util::money(r.amount), true);
        amt->setFont(bold);
        if (!dim) amt->setForeground(r.type == TxType::Income ? th.goodText : th.text);
        QString sched = recurrence::describe(r);
        if (r.endDate.isValid()) sched += QStringLiteral(" · until %1").arg(util::fmtDate(r.endDate));
        set(ColSchedule, sched);
        QString next;
        if (r.finished()) next = QStringLiteral("Ended");
        else if (!r.active) next = QStringLiteral("Paused");
        else next = QStringLiteral("%1 · %2").arg(util::fmtDateLong(r.nextDate), dueText(r.nextDate));
        auto *nx = set(ColNext, next);
        if (!dim && today.daysTo(r.nextDate) <= 3) nx->setFont(bold);
        set(ColRecorded, QString::number(s.recurringPostedCount(r.id)), true);
        if (r.id == prev) sel = i;
    }
    if (sel >= 0) m_table->selectRow(sel);
    updateButtons();
}

void RecurringPage::addRule()
{
    RecurringDialog dlg(this);
    dlg.exec();
}

void RecurringPage::editRule(const QString &id)
{
    const Recurring *r = Store::instance().recurringRule(id);
    if (!r) return;
    RecurringDialog dlg(this);
    dlg.setRule(*r);
    dlg.exec();
}

void RecurringPage::togglePause(const QString &id)
{
    const Recurring *r = Store::instance().recurringRule(id);
    if (!r) return;
    if (r->active && QMessageBox::question(this, QStringLiteral("Pause"),
                                           QStringLiteral("Pause “%1”? No payments are recorded while it's paused; when you resume, it continues from the next due date.")
                                               .arg(r->title())) != QMessageBox::Yes)
        return;
    Store::instance().setRecurringActive(id, !r->active);
}

void RecurringPage::deleteRule(const QString &id)
{
    const Recurring *r = Store::instance().recurringRule(id);
    if (!r) return;
    const int n = Store::instance().recurringPostedCount(id);
    if (QMessageBox::question(this, QStringLiteral("Delete recurring transaction"),
                              QStringLiteral("Stop “%1” and delete this recurring rule?\n\nThe %2 payment(s) already recorded will be kept.").arg(r->title()).arg(n))
        != QMessageBox::Yes)
        return;
    QString err;
    if (!Store::instance().deleteRecurring(id, &err)) QMessageBox::warning(this, QStringLiteral("Delete failed"), err);
}
