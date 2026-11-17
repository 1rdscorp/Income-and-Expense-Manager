#include "DashboardPage.h"

#include "BarChart.h"
#include "MainWindow.h"
#include "Theme.h"
#include "Widgets.h"
#include "core/Recurrence.h"
#include "core/Store.h"
#include "core/Util.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QLocale>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

namespace {

QString deltaHtml(qint64 cur, qint64 prev, bool upIsGood)
{
    if (!prev) return cur ? QStringLiteral("No data last month") : QString();
    const double d = (static_cast<double>(cur - prev) / std::fabs(static_cast<double>(prev))) * 100.0;
    const bool up = d >= 0;
    const bool good = up == upIsGood;
    const QString arrow = up ? QStringLiteral("▲") : QStringLiteral("▼");
    const QString txt = QStringLiteral("%1 %2%").arg(arrow).arg(std::fabs(d), 0, 'f', std::fabs(d) < 10 ? 1 : 0);
    if (std::fabs(d) < 0.5) return txt.toHtmlEscaped() + QStringLiteral(" vs last month");
    return ui::colorSpan(txt, good ? Theme::c().goodText : Theme::c().badText, true) + QStringLiteral(" vs last month");
}

} // namespace

DashboardPage::DashboardPage(QWidget *parent)
    : QWidget(parent)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(26, 20, 26, 26);
    root->setSpacing(16);

    m_banner = new Banner;
    m_banner->button()->setText(QStringLiteral("Back up now"));
    m_banner->button()->setProperty("iconName", QStringLiteral("download"));
    connect(m_banner->button(), &QPushButton::clicked, this, &DashboardPage::requestBackup);
    root->addWidget(m_banner);

    // Notice for payments recorded automatically by recurring rules
    m_autoBanner = new QFrame;
    m_autoBanner->setObjectName(QStringLiteral("infoBanner"));
    auto *ab = new QHBoxLayout(m_autoBanner);
    ab->setContentsMargins(14, 10, 14, 10);
    ab->setSpacing(12);
    auto *abIcon = new QLabel;
    abIcon->setObjectName(QStringLiteral("autoIcon"));
    m_autoText = new QLabel;
    m_autoText->setWordWrap(true);
    m_autoText->setTextFormat(Qt::RichText);
    auto *abView = ui::button(QStringLiteral("View"));
    auto *abDismiss = ui::button(QStringLiteral("Dismiss"));
    ab->addWidget(abIcon, 0, Qt::AlignTop);
    ab->addWidget(m_autoText, 1);
    ab->addWidget(abView);
    ab->addWidget(abDismiss);
    connect(abView, &QPushButton::clicked, this, [this]() { emit requestPage(MainWindow::Transactions); });
    connect(abDismiss, &QPushButton::clicked, this, [this]() {
        Store::instance().clearAutoPosted();
        refresh();
    });
    root->addWidget(m_autoBanner);

    // Empty state
    auto *empty = new Card;
    m_empty = empty;
    auto *el = empty->body();
    el->setContentsMargins(20, 30, 20, 30);
    auto *emoji = new QLabel(QStringLiteral("📒"));
    emoji->setAlignment(Qt::AlignCenter);
    emoji->setStyleSheet(QStringLiteral("font-size:40px;"));
    auto *welcome = new QLabel;
    welcome->setObjectName(QStringLiteral("emptyTitle"));
    welcome->setAlignment(Qt::AlignCenter);
    welcome->setStyleSheet(QStringLiteral("font-size:17px;font-weight:600;"));
    auto *blurb = ui::muted(QStringLiteral("Record your daily income and expenses, attach receipts, and see monthly and yearly totals. "
                                           "Standard business categories are ready to use."));
    blurb->setAlignment(Qt::AlignCenter);
    blurb->setStyleSheet(QStringLiteral("font-size:13px;"));
    auto *btns = new QHBoxLayout;
    btns->addStretch(1);
    auto *add = ui::button(QStringLiteral("Add your first transaction"), QStringLiteral("plus"), QStringLiteral("primary"));
    auto *sample = ui::button(QStringLiteral("Load sample data"));
    btns->addWidget(add);
    btns->addWidget(sample);
    btns->addStretch(1);
    el->addWidget(emoji);
    el->addWidget(welcome);
    el->addWidget(blurb);
    el->addSpacing(8);
    el->addLayout(btns);
    connect(add, &QPushButton::clicked, this, &DashboardPage::requestAdd);
    connect(sample, &QPushButton::clicked, this, [this]() {
        if (QMessageBox::question(this, QStringLiteral("Load sample data"),
                                  QStringLiteral("Add about six months of example transactions?\nYou can remove them later with Settings → Erase all data."))
            == QMessageBox::Yes)
            Store::instance().loadSampleData();
    });
    root->addWidget(m_empty);

    m_content = new QWidget;
    auto *cl = new QVBoxLayout(m_content);
    cl->setContentsMargins(0, 0, 0, 0);
    cl->setSpacing(16);

    auto *kpiRow = new QHBoxLayout;
    kpiRow->setSpacing(12);
    for (int i = 0; i < 5; ++i) {
        auto *k = new KpiCard;
        m_kpis << k;
        kpiRow->addWidget(k);
    }
    cl->addLayout(kpiRow);

    auto *row2 = new QHBoxLayout;
    row2->setSpacing(16);
    m_chartCard = new Card(QStringLiteral("Income vs expenses"));
    auto *legend = new QLabel;
    legend->setObjectName(QStringLiteral("legend"));
    legend->setTextFormat(Qt::RichText);
    m_chartCard->header()->addWidget(legend);
    m_chart = new BarChart;
    m_chart->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_chartCard->body()->addWidget(m_chart);
    m_chartCard->body()->addWidget(ui::muted(QStringLiteral("Click a month to see its transactions.")));
    connect(m_chart, &BarChart::columnClicked, this, [this](int i) { emit monthClicked(m_chartYear, i + 1); });
    row2->addWidget(m_chartCard, 17);

    m_topCard = new Card(QStringLiteral("Top spending"));
    auto *full = new QPushButton(QStringLiteral("Full report"));
    full->setObjectName(QStringLiteral("link"));
    full->setCursor(Qt::PointingHandCursor);
    connect(full, &QPushButton::clicked, this, [this]() { emit requestPage(MainWindow::Reports); });
    m_topCard->header()->addWidget(full);
    m_top = new HBarList;
    m_topCard->body()->addWidget(m_top);
    m_topCard->body()->addStretch(1);
    row2->addWidget(m_topCard, 10);
    cl->addLayout(row2);

    auto *row3 = new QHBoxLayout;
    row3->setSpacing(16);
    auto *recentCard = new Card(QStringLiteral("Recent transactions"));
    auto *all = new QPushButton(QStringLiteral("View all"));
    all->setObjectName(QStringLiteral("link"));
    all->setCursor(Qt::PointingHandCursor);
    connect(all, &QPushButton::clicked, this, [this]() { emit requestPage(MainWindow::Transactions); });
    recentCard->header()->addWidget(all);
    m_recent = new QTreeWidget;
    m_recent->setColumnCount(4);
    m_recent->setHeaderLabels({QStringLiteral("Date"), QStringLiteral("Description"), QStringLiteral("Category"), QStringLiteral("Amount")});
    m_recent->setRootIsDecorated(false);
    m_recent->setUniformRowHeights(true);
    m_recent->setSelectionMode(QAbstractItemView::SingleSelection);
    m_recent->header()->setStretchLastSection(false);
    m_recent->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_recent->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_recent->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_recent->header()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_recent->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_recent->setToolTip(QStringLiteral("Double-click to edit"));
    connect(m_recent, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *it) { emit requestEdit(it->data(0, Qt::UserRole).toString()); });
    recentCard->body()->addWidget(m_recent);
    recentCard->body()->addStretch(1);
    row3->addWidget(recentCard, 1);

    auto *rightCol = new QVBoxLayout;
    rightCol->setSpacing(16);
    auto *upCard = new Card(QStringLiteral("Upcoming"), QStringLiteral("Next 30 days"));
    auto *manage = new QPushButton(QStringLiteral("Manage recurring"));
    manage->setObjectName(QStringLiteral("link"));
    manage->setCursor(Qt::PointingHandCursor);
    connect(manage, &QPushButton::clicked, this, [this]() { emit requestPage(MainWindow::Recurring); });
    upCard->header()->addWidget(manage);
    m_upcoming = new QTreeWidget;
    m_upcoming->setColumnCount(3);
    m_upcoming->setHeaderHidden(true);
    m_upcoming->setRootIsDecorated(false);
    m_upcoming->setUniformRowHeights(true);
    m_upcoming->setSelectionMode(QAbstractItemView::NoSelection);
    m_upcoming->setFocusPolicy(Qt::NoFocus);
    m_upcoming->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_upcoming->header()->setStretchLastSection(false);
    m_upcoming->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_upcoming->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_upcoming->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    upCard->body()->addWidget(m_upcoming);
    m_upcomingEmpty = ui::muted(QStringLiteral("No bills due. Add rent, internet and other regular payments on the Recurring page "
                                               "and they'll be recorded automatically."));
    upCard->body()->addWidget(m_upcomingEmpty);
    rightCol->addWidget(upCard);

    auto *weekCard = new Card(QStringLiteral("Last 7 days"), QStringLiteral("Daily totals"));
    m_week = new HBarList;
    weekCard->body()->addWidget(m_week);
    weekCard->body()->addWidget(ui::muted(QStringLiteral("Bars show expenses; income shown in green.")));
    weekCard->body()->addStretch(1);
    rightCol->addWidget(weekCard, 1);
    row3->addLayout(rightCol, 1);
    cl->addLayout(row3);
    cl->addStretch(1);

    root->addWidget(m_content);
    root->addStretch(1);
}

void DashboardPage::refresh()
{
    const Store &s = Store::instance();
    const auto &t = Theme::c();
    const QDate today = QDate::currentDate();
    const int y = today.year(), m = today.month();
    const QDate prevMonth = QDate(y, m, 1).addMonths(-1);
    const Totals mt = Store::totals(s.inMonth(y, m));
    const Totals pt = Store::totals(s.inMonth(prevMonth.year(), prevMonth.month()));
    const Totals yt = Store::totals(s.inYear(y));
    const Totals td = Store::totals(s.onDay(today));
    const QString mon = util::monthLabel(y, m, true);

    // Backup reminder
    m_banner->setVisible(s.backupIsStale());
    const QString last = s.settings().lastBackupAt;
    m_banner->setText(QStringLiteral("<b>Time for a backup.</b> %1 Your data lives only on this computer — keep a copy somewhere safe.")
                          .arg(last.isEmpty() ? QStringLiteral("You haven't backed up your data yet.")
                                              : QStringLiteral("Your last backup was on %1.").arg(util::fmtDateTime(last))));

    const auto &autoPosted = s.autoPosted();
    m_autoBanner->setVisible(!autoPosted.isEmpty());
    if (!autoPosted.isEmpty()) {
        if (auto *ic = m_autoBanner->findChild<QLabel *>(QStringLiteral("autoIcon"))) ic->setPixmap(Theme::iconPixmap(QStringLiteral("repeat"), t.accent, 20));
        QStringList items;
        qint64 total = 0;
        for (int i = 0; i < autoPosted.size(); ++i) {
            const Transaction &x = autoPosted[i];
            total += x.type == TxType::Income ? x.amount : -x.amount;
            if (i < 3) {
                const QString name = x.description.isEmpty() ? s.categoryOrUnknown(x.categoryId).name : x.description;
                items << QStringLiteral("%1 %2 (%3)").arg(name.toHtmlEscaped(), util::money(x.amount), QLocale().toString(x.date, QStringLiteral("d MMM")));
            }
        }
        if (autoPosted.size() > 3) items << QStringLiteral("%1 more").arg(autoPosted.size() - 3);
        m_autoText->setText(QStringLiteral("<b>%1 recurring payment%2 recorded automatically:</b> %3")
                                .arg(autoPosted.size())
                                .arg(autoPosted.size() == 1 ? QString() : QStringLiteral("s"), items.join(QStringLiteral(", "))));
    }

    const bool empty = s.transactions().isEmpty();
    m_empty->setVisible(empty);
    m_content->setVisible(!empty);
    if (auto *w = m_empty->findChild<QLabel *>(QStringLiteral("emptyTitle")))
        w->setText(QStringLiteral("Welcome to %1").arg(s.settings().businessName));
    if (empty) return;

    m_kpis[0]->set(QStringLiteral("Spent today"), util::money(td.expense),
                   td.count ? QStringLiteral("%1 transaction%2 today%3").arg(td.count).arg(td.count > 1 ? QStringLiteral("s") : QString())
                                  .arg(td.income ? QStringLiteral(" · %1 received").arg(util::money(td.income)) : QString())
                            : QStringLiteral("Nothing recorded yet today"));
    m_kpis[1]->set(QStringLiteral("Income · %1").arg(mon), util::money(mt.income), deltaHtml(mt.income, pt.income, true), t.income);
    m_kpis[2]->set(QStringLiteral("Expenses · %1").arg(mon), util::money(mt.expense), deltaHtml(mt.expense, pt.expense, false), t.expense);
    m_kpis[3]->set(QStringLiteral("Net profit · %1").arg(mon), util::signedMoney(mt.net()),
                   mt.income ? QStringLiteral("%1 margin").arg(util::pctText(mt.net(), mt.income)) : QStringLiteral("No income yet this month"),
                   QColor(), mt.net() < 0);
    m_kpis[4]->set(QStringLiteral("Net profit · %1 to date").arg(y), util::signedMoney(yt.net()),
                   QStringLiteral("%1 in · %2 out").arg(util::moneyCompact(yt.income), util::moneyCompact(yt.expense)), QColor(), yt.net() < 0);

    // Chart
    m_chartYear = y;
    m_chartCard->setSubtitle(QStringLiteral("· %1").arg(y));
    if (auto *legend = m_chartCard->findChild<QLabel *>(QStringLiteral("legend")))
        legend->setText(QStringLiteral("<span style='color:%1'>■</span> Income &nbsp;&nbsp;<span style='color:%2'>■</span> Expenses")
                            .arg(t.income.name(), t.expense.name()));
    const auto series = s.monthly(y);
    QStringList labels, full;
    QVector<qint64> inc, exp;
    for (int i = 0; i < 12; ++i) {
        labels << util::monthName(i + 1, true);
        full << util::monthLabel(y, i + 1);
        inc << series[static_cast<size_t>(i)].income;
        exp << series[static_cast<size_t>(i)].expense;
    }
    m_chart->setData(labels, full, inc, exp);

    // Top spending this month
    QVector<Transaction> monthExp;
    for (const auto &x : s.inMonth(y, m))
        if (x.type == TxType::Expense) monthExp.push_back(x);
    const auto bc = Store::byCategory(monthExp);
    struct R {
        Category c;
        CategoryTotal v;
    };
    QVector<R> rows;
    for (auto it = bc.begin(); it != bc.end(); ++it) rows.push_back({s.categoryOrUnknown(it.key()), it.value()});
    std::sort(rows.begin(), rows.end(), [](const R &a, const R &b) { return a.v.total > b.v.total; });
    qint64 maxV = 1, rest = 0;
    for (int i = 0; i < rows.size(); ++i) {
        if (i < 6) maxV = std::max({maxV, rows[i].v.total, rows[i].c.budget});
        else rest += rows[i].v.total;
    }
    maxV = std::max(maxV, rest);
    QVector<HBarList::Row> bars;
    auto barFor = [&](const Category &c, qint64 total, int count) {
        HBarList::Row r;
        r.icon = c.icon;
        r.name = c.name;
        r.amount = util::money(total);
        r.fraction = static_cast<double>(total) / static_cast<double>(maxV);
        QStringList sub;
        if (count >= 0) sub << QStringLiteral("%1 transaction%2").arg(count).arg(count == 1 ? QString() : QStringLiteral("s"));
        if (mt.expense) sub << QStringLiteral("%1 of spending").arg(util::pctText(total, mt.expense));
        r.sub = sub.join(QStringLiteral(" · "));
        if (c.budget > 0) {
            r.over = total > c.budget;
            r.warn = r.over ? QStringLiteral("⚠ Over budget by %1").arg(util::money(total - c.budget))
                            : QStringLiteral("%1 left of %2 budget").arg(util::money(c.budget - total), util::money(c.budget));
        }
        return r;
    };
    for (int i = 0; i < std::min<int>(6, rows.size()); ++i) bars << barFor(rows[i].c, rows[i].v.total, rows[i].v.count);
    if (rest) {
        Category other;
        other.name = QStringLiteral("Other (%1 categories)").arg(rows.size() - 6);
        other.icon = QStringLiteral("…");
        bars << barFor(other, rest, -1);
    }
    for (const auto &c : s.categoriesOfType(TxType::Expense)) {
        if (bars.size() >= 7) break;
        if (c.budget > 0 && !bc.contains(c.id)) bars << barFor(c, 0, 0);
    }
    m_topCard->setSubtitle(QStringLiteral("· %1").arg(util::monthLabel(y, m)));
    m_top->setRows(bars, QStringLiteral("No expenses recorded in %1 yet.").arg(util::monthLabel(y, m)));

    // Recent
    m_recent->clear();
    const auto &txs = s.transactions();
    for (int i = 0; i < std::min<int>(8, txs.size()); ++i) {
        const Transaction &x = txs[i];
        const Category c = s.categoryOrUnknown(x.categoryId);
        auto *it = new QTreeWidgetItem(m_recent);
        it->setText(0, util::fmtDate(x.date));
        QString desc = x.description.isEmpty() ? (x.vendor.isEmpty() ? c.name : x.vendor) : x.description;
        if (s.receiptCount(x.id)) desc += QStringLiteral("  📎");
        it->setText(1, desc);
        it->setText(2, c.icon + QStringLiteral("  ") + c.name);
        it->setText(3, (x.type == TxType::Income ? QStringLiteral("+") : QStringLiteral("−")) + util::money(x.amount));
        it->setTextAlignment(3, Qt::AlignRight | Qt::AlignVCenter);
        it->setForeground(3, x.type == TxType::Income ? t.goodText : t.text);
        QFont f = it->font(3);
        f.setWeight(QFont::DemiBold);
        it->setFont(3, f);
        it->setData(0, Qt::UserRole, x.id);
        it->setForeground(0, t.muted);
    }
    const int rowH = m_recent->sizeHintForRow(0) > 0 ? m_recent->sizeHintForRow(0) : 26;
    m_recent->setFixedHeight(m_recent->header()->sizeHint().height() + rowH * m_recent->topLevelItemCount() + 6);

    // Last 7 days
    QVector<HBarList::Row> days;
    qint64 weekMax = 1;
    QVector<Totals> dt;
    for (int i = 0; i < 7; ++i) {
        dt << Store::totals(s.onDay(today.addDays(-i)));
        weekMax = std::max(weekMax, dt.last().expense);
    }
    for (int i = 0; i < 7; ++i) {
        HBarList::Row r;
        r.name = util::fmtDay(today.addDays(-i));
        r.amount = util::money(dt[i].expense);
        r.extra = dt[i].income ? QStringLiteral("+%1").arg(util::money(dt[i].income)) : QString();
        r.fraction = static_cast<double>(dt[i].expense) / static_cast<double>(weekMax);
        days << r;
    }
    m_week->setRows(days);

    // Upcoming recurring payments
    m_upcoming->clear();
    const auto up = s.upcoming(today, today.addDays(30));
    for (int i = 0; i < std::min<int>(6, up.size()); ++i) {
        const QDate &d = up[i].first;
        const Recurring &r = up[i].second;
        const Category c = s.categoryOrUnknown(r.categoryId);
        const qint64 in = today.daysTo(d);
        auto *it = new QTreeWidgetItem(m_upcoming);
        it->setText(0, in == 0 ? QStringLiteral("Today") : in == 1 ? QStringLiteral("Tomorrow") : QLocale().toString(d, QStringLiteral("ddd d MMM")));
        it->setToolTip(0, in > 1 ? QStringLiteral("in %1 days").arg(in) : QString());
        it->setForeground(0, in <= 3 ? t.text : t.muted);
        it->setText(1, c.icon + QStringLiteral("  ") + (r.title().isEmpty() ? c.name : r.title()));
        it->setToolTip(1, recurrence::describe(r));
        it->setText(2, (r.type == TxType::Income ? QStringLiteral("+") : QStringLiteral("−")) + util::money(r.amount));
        it->setTextAlignment(2, Qt::AlignRight | Qt::AlignVCenter);
        it->setForeground(2, r.type == TxType::Income ? t.goodText : t.text);
        QFont f = it->font(2);
        f.setWeight(QFont::DemiBold);
        it->setFont(2, f);
    }
    if (up.size() > 6) {
        auto *more = new QTreeWidgetItem(m_upcoming);
        more->setText(1, QStringLiteral("+ %1 more").arg(up.size() - 6));
        more->setForeground(1, t.muted);
    }
    m_upcoming->setVisible(!up.isEmpty());
    m_upcomingEmpty->setVisible(up.isEmpty());
    const int upRowH = m_upcoming->sizeHintForRow(0) > 0 ? m_upcoming->sizeHintForRow(0) : 26;
    m_upcoming->setFixedHeight(upRowH * m_upcoming->topLevelItemCount() + 6);
}
