#include "Reports.h"
#include "Store.h"
#include "Util.h"

#include <QDateTime>
#include <QLocale>
#include <QMap>
#include <array>
#include <algorithm>

namespace reports {

using util::amountPlain;
using util::money;

namespace {

struct CatRow {
    Category c;
    CategoryTotal v;
};

QVector<CatRow> categoryRows(const Store &store, const QVector<Transaction> &txs, TxType type)
{
    QVector<Transaction> sub;
    for (const auto &t : txs)
        if (t.type == type) sub.push_back(t);
    const auto bc = Store::byCategory(sub);
    QVector<CatRow> rows;
    for (auto it = bc.begin(); it != bc.end(); ++it) rows.push_back({store.categoryOrUnknown(it.key()), it.value()});
    std::sort(rows.begin(), rows.end(), [](const CatRow &a, const CatRow &b) { return a.v.total > b.v.total; });
    return rows;
}

QString catLabel(const Category &c) { return c.icon.isEmpty() ? c.name : c.icon + QStringLiteral("  ") + c.name; }

Table categoryTable(const QString &title, const QVector<CatRow> &rows, qint64 grand, bool withBudget)
{
    Table t;
    t.title = title;
    t.half = false;
    t.subtitle = QStringLiteral("%1 categor%2").arg(rows.size()).arg(rows.size() == 1 ? QStringLiteral("y") : QStringLiteral("ies"));
    t.headers = {QStringLiteral("Category"), QStringLiteral("Count"), QStringLiteral("Amount"), QStringLiteral("% of total")};
    t.numericColumns = {1, 2, 3};
    bool showBudget = false;
    if (withBudget)
        for (const auto &r : rows)
            if (r.c.budget > 0) showBudget = true;
    if (showBudget) {
        t.headers << QStringLiteral("Budget") << QStringLiteral("Remaining");
        t.numericColumns << 4 << 5;
    }
    t.emptyText = QStringLiteral("None recorded.");
    if (rows.isEmpty()) return t;
    int count = 0;
    for (const auto &r : rows) {
        QStringList cells{catLabel(r.c), QString::number(r.v.count), money(r.v.total), util::pctText(r.v.total, grand)};
        if (showBudget) {
            cells << (r.c.budget ? money(r.c.budget) : QStringLiteral("—"))
                  << (r.c.budget ? util::signedMoney(r.c.budget - r.v.total) : QStringLiteral("—"));
        }
        t.rows << cells;
        t.kinds << RowKind::Normal;
        count += r.v.count;
    }
    QStringList total{QStringLiteral("Total"), QString::number(count), money(grand), QStringLiteral("100%")};
    if (showBudget) total << QString() << QString();
    t.rows << total;
    t.kinds << RowKind::Total;
    return t;
}

QString marginText(const Totals &t)
{
    return t.income ? QStringLiteral("%1 profit margin").arg(util::pctText(t.net(), t.income)) : QStringLiteral("—");
}

void addSummaryStats(Report &r, const Totals &t)
{
    r.stats.push_back({QStringLiteral("Total income"), money(t.income), QString(), QStringLiteral("income"), false});
    r.stats.push_back({QStringLiteral("Total expenses"), money(t.expense), QString(), QStringLiteral("expense"), false});
    r.stats.push_back({QStringLiteral("Net profit"), util::signedMoney(t.net()), marginText(t), QString(), t.net() < 0});
}

} // namespace

Report monthly(const Store &store, int year, int month)
{
    Report r;
    r.kind = QStringLiteral("monthly");
    r.title = QStringLiteral("Monthly report");
    r.period = util::monthLabel(year, month);
    const auto txs = store.inMonth(year, month);
    const Totals t = Store::totals(txs);

    const QDate first(year, month, 1);
    const QDate today = QDate::currentDate();
    int elapsed = 0;
    if (first.year() == today.year() && first.month() == today.month()) elapsed = today.day();
    else if (first < today) elapsed = first.daysInMonth();
    addSummaryStats(r, t);
    r.stats.push_back({QStringLiteral("Avg. daily spending"), money(elapsed ? t.expense / elapsed : 0),
                       QStringLiteral("%1 transactions").arg(t.count), QString(), false});

    const auto inc = categoryRows(store, txs, TxType::Income);
    const auto exp = categoryRows(store, txs, TxType::Expense);
    r.tables << categoryTable(QStringLiteral("Income by category"), inc, t.income, false);
    r.tables << categoryTable(QStringLiteral("Expenses by category"), exp, t.expense, true);

    // Daily breakdown
    QMap<QDate, Totals> days;
    for (const auto &x : txs) {
        auto &d = days[x.date];
        if (x.type == TxType::Income) d.income += x.amount;
        else d.expense += x.amount;
        ++d.count;
    }
    Table daily;
    daily.title = QStringLiteral("Daily breakdown");
    daily.subtitle = QStringLiteral("%1 day%2 with activity").arg(days.size()).arg(days.size() == 1 ? QString() : QStringLiteral("s"));
    daily.headers = {QStringLiteral("Date"), QStringLiteral("Transactions"), QStringLiteral("Income"), QStringLiteral("Expenses"), QStringLiteral("Net")};
    daily.numericColumns = {1, 2, 3, 4};
    daily.emptyText = QStringLiteral("No transactions this month.");
    for (auto it = days.begin(); it != days.end(); ++it) {
        daily.rows << QStringList{util::fmtDateLong(it.key()), QString::number(it->count), money(it->income), money(it->expense), util::signedMoney(it->net())};
        daily.kinds << RowKind::Normal;
    }
    if (!days.isEmpty()) {
        daily.rows << QStringList{QStringLiteral("Total"), QString::number(t.count), money(t.income), money(t.expense), util::signedMoney(t.net())};
        daily.kinds << RowKind::Total;
    }
    r.tables << daily;

    // CSV
    r.csv << QStringList{QStringLiteral("%1 — Monthly report").arg(store.settings().businessName), r.period} << QStringList{}
          << QStringList{QStringLiteral("Summary")} << QStringList{QStringLiteral("Total income"), amountPlain(t.income)}
          << QStringList{QStringLiteral("Total expenses"), amountPlain(t.expense)} << QStringList{QStringLiteral("Net profit"), amountPlain(t.net())}
          << QStringList{} << QStringList{QStringLiteral("Type"), QStringLiteral("Category"), QStringLiteral("Transactions"), QStringLiteral("Amount"), QStringLiteral("% of type total")};
    for (const auto &x : inc) r.csv << QStringList{QStringLiteral("Income"), x.c.name, QString::number(x.v.count), amountPlain(x.v.total), QString::number(util::pct(x.v.total, t.income))};
    for (const auto &x : exp) r.csv << QStringList{QStringLiteral("Expense"), x.c.name, QString::number(x.v.count), amountPlain(x.v.total), QString::number(util::pct(x.v.total, t.expense))};
    r.csv << QStringList{} << QStringList{QStringLiteral("Date"), QStringLiteral("Transactions"), QStringLiteral("Income"), QStringLiteral("Expenses"), QStringLiteral("Net")};
    for (auto it = days.begin(); it != days.end(); ++it)
        r.csv << QStringList{it.key().toString(Qt::ISODate), QString::number(it->count), amountPlain(it->income), amountPlain(it->expense), amountPlain(it->net())};
    return r;
}

Report annual(const Store &store, int year)
{
    Report r;
    r.kind = QStringLiteral("annual");
    r.title = QStringLiteral("Annual report");
    r.period = QString::number(year);
    const auto txs = store.inYear(year);
    const Totals t = Store::totals(txs);
    const auto series = store.monthly(year);
    int active = 0;
    for (const auto &m : series)
        if (m.income || m.expense) ++active;
    addSummaryStats(r, t);
    r.stats.push_back({QStringLiteral("Avg. monthly expenses"), money(active ? t.expense / active : 0),
                       QStringLiteral("over %1 active month%2").arg(active).arg(active == 1 ? QString() : QStringLiteral("s")), QString(), false});

    r.chart.valid = true;
    r.chart.title = QStringLiteral("Monthly totals · %1").arg(year);
    for (int i = 0; i < 12; ++i) {
        r.chart.labels << util::monthName(i + 1, true);
        r.chart.fullLabels << util::monthLabel(year, i + 1);
        r.chart.income << series[static_cast<size_t>(i)].income;
        r.chart.expense << series[static_cast<size_t>(i)].expense;
    }

    struct MRow {
        Category c;
        std::array<qint64, 12> m{};
        qint64 total = 0;
    };
    auto matrix = [&](TxType type) {
        QHash<QString, MRow> h;
        for (const auto &x : txs) {
            if (x.type != type) continue;
            auto &row = h[x.categoryId];
            row.c = store.categoryOrUnknown(x.categoryId);
            row.m[static_cast<size_t>(x.date.month() - 1)] += x.amount;
            row.total += x.amount;
        }
        QVector<MRow> rows(h.begin(), h.end());
        std::sort(rows.begin(), rows.end(), [](const MRow &a, const MRow &b) { return a.total > b.total; });
        return rows;
    };
    const auto inc = matrix(TxType::Income);
    const auto exp = matrix(TxType::Expense);
    auto cell = [](qint64 v) { return v ? money(v) : QStringLiteral("–"); };

    Table m;
    m.title = QStringLiteral("Category by month · %1").arg(year);
    m.headers << QStringLiteral("Category");
    for (int i = 1; i <= 12; ++i) {
        m.headers << util::monthName(i, true);
        m.numericColumns << i;
    }
    m.headers << QStringLiteral("Total");
    m.numericColumns << 13;
    m.emptyText = QStringLiteral("No transactions this year.");
    if (!txs.isEmpty()) {
        auto section = [&](const QString &title, const QVector<MRow> &rows, bool income) {
            QStringList s{title};
            for (int i = 0; i < 13; ++i) s << QString();
            m.rows << s;
            m.kinds << RowKind::Section;
            for (const auto &row : rows) {
                QStringList cells{catLabel(row.c)};
                for (qint64 v : row.m) cells << cell(v);
                cells << money(row.total);
                m.rows << cells;
                m.kinds << RowKind::Normal;
            }
            QStringList tot{QStringLiteral("Total %1").arg(title.toLower())};
            for (const auto &mm : series) tot << cell(income ? mm.income : mm.expense);
            tot << money(income ? t.income : t.expense);
            m.rows << tot;
            m.kinds << RowKind::Total;
        };
        section(QStringLiteral("Income"), inc, true);
        section(QStringLiteral("Expenses"), exp, false);
        QStringList net{QStringLiteral("Net profit")};
        for (const auto &mm : series) net << ((mm.income || mm.expense) ? util::signedMoney(mm.net()) : QStringLiteral("–"));
        net << util::signedMoney(t.net());
        m.rows << net;
        m.kinds << RowKind::Net;
    }
    r.tables << m;

    QStringList head{QStringLiteral("Category")};
    for (int i = 1; i <= 12; ++i) head << util::monthName(i);
    head << QStringLiteral("Total");
    r.csv << QStringList{QStringLiteral("%1 — Annual report").arg(store.settings().businessName), r.period} << QStringList{}
          << QStringList{QStringLiteral("Income")} << head;
    for (const auto &row : inc) {
        QStringList c{row.c.name};
        for (qint64 v : row.m) c << amountPlain(v);
        r.csv << (c << amountPlain(row.total));
    }
    QStringList ti{QStringLiteral("Total income")};
    for (const auto &mm : series) ti << amountPlain(mm.income);
    r.csv << (ti << amountPlain(t.income)) << QStringList{} << QStringList{QStringLiteral("Expenses")} << head;
    for (const auto &row : exp) {
        QStringList c{row.c.name};
        for (qint64 v : row.m) c << amountPlain(v);
        r.csv << (c << amountPlain(row.total));
    }
    QStringList te{QStringLiteral("Total expenses")};
    for (const auto &mm : series) te << amountPlain(mm.expense);
    r.csv << (te << amountPlain(t.expense)) << QStringList{};
    QStringList tn{QStringLiteral("Net profit")};
    for (const auto &mm : series) tn << amountPlain(mm.net());
    r.csv << (tn << amountPlain(t.net()));
    return r;
}

Report yearOverYear(const Store &store)
{
    Report r;
    r.kind = QStringLiteral("yoy");
    r.title = QStringLiteral("Year-over-year report");
    r.period = QStringLiteral("All years");
    struct Y {
        int y;
        Totals t;
        QString top;
    };
    QVector<Y> data;
    for (int y : store.yearsWithData()) {
        const auto txs = store.inYear(y);
        const auto exp = categoryRows(store, txs, TxType::Expense);
        data.push_back({y, Store::totals(txs), exp.isEmpty() ? QStringLiteral("—") : exp.first().c.name});
    }
    r.chart.valid = true;
    r.chart.title = QStringLiteral("Totals by year");
    for (const auto &d : data) {
        r.chart.labels << QString::number(d.y);
        r.chart.fullLabels << QString::number(d.y);
        r.chart.income << d.t.income;
        r.chart.expense << d.t.expense;
    }
    auto change = [](qint64 cur, qint64 prev) -> QString {
        if (!prev) return QStringLiteral("—");
        const double d = (static_cast<double>(cur - prev) / static_cast<double>(prev < 0 ? -prev : prev)) * 100.0;
        return QStringLiteral("%1 %2%").arg(cur >= prev ? QStringLiteral("▲") : QStringLiteral("▼"), QLocale().toString(d < 0 ? -d : d, 'f', 1));
    };
    Table t;
    t.title = QStringLiteral("Year over year");
    t.headers = {QStringLiteral("Year"), QStringLiteral("Income"), QStringLiteral("Change"), QStringLiteral("Expenses"), QStringLiteral("Change"),
                 QStringLiteral("Net profit"), QStringLiteral("Margin"), QStringLiteral("Transactions"), QStringLiteral("Largest expense")};
    t.numericColumns = {1, 2, 3, 4, 5, 6, 7};
    for (int i = data.size() - 1; i >= 0; --i) {
        const auto &d = data[i];
        const Y *prev = i > 0 && data[i - 1].y == d.y - 1 ? &data[i - 1] : nullptr;
        t.rows << QStringList{QString::number(d.y), money(d.t.income), prev ? change(d.t.income, prev->t.income) : QStringLiteral("—"),
                              money(d.t.expense), prev ? change(d.t.expense, prev->t.expense) : QStringLiteral("—"),
                              util::signedMoney(d.t.net()), d.t.income ? util::pctText(d.t.net(), d.t.income) : QStringLiteral("—"),
                              QString::number(d.t.count), d.top};
        t.kinds << RowKind::Normal;
    }
    r.tables << t;
    r.csv << QStringList{QStringLiteral("%1 — Year over year").arg(store.settings().businessName)} << QStringList{}
          << QStringList{QStringLiteral("Year"), QStringLiteral("Income"), QStringLiteral("Expenses"), QStringLiteral("Net profit"),
                         QStringLiteral("Margin %"), QStringLiteral("Transactions"), QStringLiteral("Largest expense category")};
    for (const auto &d : data)
        r.csv << QStringList{QString::number(d.y), amountPlain(d.t.income), amountPlain(d.t.expense), amountPlain(d.t.net()),
                             d.t.income ? QString::number(util::pct(d.t.net(), d.t.income)) : QString(), QString::number(d.t.count), d.top};
    return r;
}

QString toHtml(const Report &r, const QString &businessName, const QString &chartImageName)
{
    auto esc = [](const QString &s) { return s.toHtmlEscaped(); };
    QString h;
    h += QStringLiteral("<html><head><style>"
                        "body{font-family:'Segoe UI',Arial,sans-serif;font-size:9pt;color:#111;}"
                        "h1{font-size:16pt;margin:0;} h2{font-size:11pt;margin:14px 0 4px 0;}"
                        ".muted{color:#666;} table{border-collapse:collapse;}"
                        "th{background:#f0efec;font-weight:600;text-align:left;padding:4px 6px;border-bottom:1px solid #bbb;}"
                        "td{padding:3px 6px;border-bottom:1px solid #e1e0d9;}"
                        "td.r,th.r{text-align:right;} tr.section td{background:#f0efec;font-weight:bold;}"
                        "tr.total td{font-weight:bold;border-top:1.5px solid #888;} tr.net td{font-weight:bold;background:#e6f0fc;}"
                        ".neg{color:#b42323;}"
                        "</style></head><body>");
    h += QStringLiteral("<h1>%1</h1><div><b>%2</b> &mdash; %3</div><div class='muted'>Generated %4</div>")
             .arg(esc(businessName), esc(r.title), esc(r.period), esc(QLocale().toString(QDateTime::currentDateTime(), QStringLiteral("d MMM yyyy, HH:mm"))));
    if (!r.stats.isEmpty()) {
        h += QStringLiteral("<br><table width='100%' cellspacing='0'><tr>");
        for (const auto &s : r.stats)
            h += QStringLiteral("<td style='border:1px solid #ddd;padding:6px'><div class='muted'>%1</div><div style='font-size:13pt;font-weight:bold' class='%4'>%2</div><div class='muted'>%3</div></td>")
                     .arg(esc(s.label), esc(s.value), esc(s.meta), s.negative ? QStringLiteral("neg") : QString());
        h += QStringLiteral("</tr></table>");
    }
    if (!chartImageName.isEmpty() && r.chart.valid)
        h += QStringLiteral("<h2>%1</h2><img src='%2' width='640'>").arg(esc(r.chart.title), esc(chartImageName));
    for (const auto &t : r.tables) {
        h += QStringLiteral("<h2>%1</h2>").arg(esc(t.title));
        if (t.rows.isEmpty()) {
            h += QStringLiteral("<p class='muted'>%1</p>").arg(esc(t.emptyText));
            continue;
        }
        h += QStringLiteral("<table width='100%' cellspacing='0'><tr>");
        for (int c = 0; c < t.headers.size(); ++c)
            h += QStringLiteral("<th class='%1'>%2</th>").arg(t.numericColumns.contains(c) ? QStringLiteral("r") : QString(), esc(t.headers[c]));
        h += QStringLiteral("</tr>");
        for (int i = 0; i < t.rows.size(); ++i) {
            const RowKind k = t.kinds.value(i, RowKind::Normal);
            const QString cls = k == RowKind::Section ? QStringLiteral("section") : k == RowKind::Total ? QStringLiteral("total") : k == RowKind::Net ? QStringLiteral("net") : QString();
            h += QStringLiteral("<tr class='%1'>").arg(cls);
            const QStringList &row = t.rows[i];
            if (k == RowKind::Section) {
                h += QStringLiteral("<td colspan='%1'>%2</td>").arg(t.headers.size()).arg(esc(row.value(0)));
            } else {
                for (int c = 0; c < row.size(); ++c) {
                    QString classes = t.numericColumns.contains(c) ? QStringLiteral("r") : QString();
                    if (row[c].startsWith(QChar(0x2212))) classes += QStringLiteral(" neg");
                    h += QStringLiteral("<td class='%1'>%2</td>").arg(classes, esc(row[c]));
                }
            }
            h += QStringLiteral("</tr>");
        }
        h += QStringLiteral("</table>");
    }
    h += QStringLiteral("</body></html>");
    return h;
}

} // namespace reports
