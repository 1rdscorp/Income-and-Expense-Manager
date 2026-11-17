#include "Reports.h"
#include "Store.h"
#include "Util.h"
#include "Xlsx.h"

#include <QDateTime>
#include <QLocale>
#include <QMap>
#include <QSaveFile>
#include <array>
#include <algorithm>
#include <cmath>

namespace reports {

using util::amountPlain;
using util::money;

QString display(const Value &v)
{
    switch (v.kind) {
    case Value::Kind::Text: return v.text;
    case Value::Kind::Money: return util::signedMoney(v.cents);
    case Value::Kind::MoneyDash: return v.cents ? util::signedMoney(v.cents) : QStringLiteral("–");
    case Value::Kind::Percent: return QLocale().toString(v.number, 'f', 1) + QLatin1Char('%');
    case Value::Kind::Change:
        return QStringLiteral("%1 %2%").arg(v.number >= 0 ? QStringLiteral("▲") : QStringLiteral("▼"), QLocale().toString(std::fabs(v.number), 'f', 1));
    case Value::Kind::Count: return QString::number(static_cast<qint64>(v.number));
    case Value::Kind::Date: return util::fmtDateLong(v.date);
    default: return QString();
    }
}

void Table::addRow(const QVector<Value> &cells, RowKind kind)
{
    QStringList text;
    for (const Value &v : cells) text << display(v);
    rows << text;
    values << cells;
    kinds << kind;
}

namespace {

Stat stat(const QString &label, const Value &raw, const QString &meta = QString(), const QString &swatch = QString())
{
    Stat s;
    s.label = label;
    s.raw = raw;
    s.value = display(raw);
    s.meta = meta;
    s.swatch = swatch;
    s.negative = (raw.kind == Value::Kind::Money || raw.kind == Value::Kind::MoneyDash) && raw.cents < 0;
    return s;
}

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
        QVector<Value> cells{Value::str(catLabel(r.c)), Value::count(r.v.count), Value::money(r.v.total), Value::percent(util::pct(r.v.total, grand))};
        if (showBudget) {
            cells << (r.c.budget ? Value::money(r.c.budget) : Value::dash()) << (r.c.budget ? Value::money(r.c.budget - r.v.total) : Value::dash());
        }
        t.addRow(cells);
        count += r.v.count;
    }
    QVector<Value> total{Value::str(QStringLiteral("Total")), Value::count(count), Value::money(grand), Value::percent(100)};
    if (showBudget) total << Value::empty() << Value::empty();
    t.addRow(total, RowKind::Total);
    return t;
}

QString marginText(const Totals &t)
{
    return t.income ? QStringLiteral("%1 profit margin").arg(util::pctText(t.net(), t.income)) : QStringLiteral("—");
}

void addSummaryStats(Report &r, const Totals &t)
{
    r.stats.push_back(stat(QStringLiteral("Total income"), Value::money(t.income), QString(), QStringLiteral("income")));
    r.stats.push_back(stat(QStringLiteral("Total expenses"), Value::money(t.expense), QString(), QStringLiteral("expense")));
    r.stats.push_back(stat(QStringLiteral("Net profit"), Value::money(t.net()), marginText(t)));
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
    r.stats.push_back(stat(QStringLiteral("Avg. daily spending"), Value::money(elapsed ? t.expense / elapsed : 0), QStringLiteral("%1 transactions").arg(t.count)));

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
    for (auto it = days.begin(); it != days.end(); ++it)
        daily.addRow({Value::day(it.key()), Value::count(it->count), Value::money(it->income), Value::money(it->expense), Value::money(it->net())});
    if (!days.isEmpty())
        daily.addRow({Value::str(QStringLiteral("Total")), Value::count(t.count), Value::money(t.income), Value::money(t.expense), Value::money(t.net())},
                     RowKind::Total);
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
    r.stats.push_back(stat(QStringLiteral("Avg. monthly expenses"), Value::money(active ? t.expense / active : 0),
                           QStringLiteral("over %1 active month%2").arg(active).arg(active == 1 ? QString() : QStringLiteral("s"))));

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
            QVector<Value> s{Value::str(title)};
            for (int i = 0; i < 13; ++i) s << Value::empty();
            m.addRow(s, RowKind::Section);
            for (const auto &row : rows) {
                QVector<Value> cells{Value::str(catLabel(row.c))};
                for (qint64 v : row.m) cells << Value::moneyDash(v);
                cells << Value::money(row.total);
                m.addRow(cells);
            }
            QVector<Value> tot{Value::str(QStringLiteral("Total %1").arg(title.toLower()))};
            for (const auto &mm : series) tot << Value::moneyDash(income ? mm.income : mm.expense);
            tot << Value::money(income ? t.income : t.expense);
            m.addRow(tot, RowKind::Total);
        };
        section(QStringLiteral("Income"), inc, true);
        section(QStringLiteral("Expenses"), exp, false);
        QVector<Value> net{Value::str(QStringLiteral("Net profit"))};
        for (const auto &mm : series) net << Value::moneyDash(mm.net());
        net << Value::money(t.net());
        m.addRow(net, RowKind::Net);
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
    auto change = [](qint64 cur, qint64 prev) -> Value {
        if (!prev) return Value::dash();
        return Value::change(std::round((static_cast<double>(cur - prev) / static_cast<double>(prev < 0 ? -prev : prev)) * 1000.0) / 10.0);
    };
    Table t;
    t.title = QStringLiteral("Year over year");
    t.headers = {QStringLiteral("Year"), QStringLiteral("Income"), QStringLiteral("Change"), QStringLiteral("Expenses"), QStringLiteral("Change"),
                 QStringLiteral("Net profit"), QStringLiteral("Margin"), QStringLiteral("Transactions"), QStringLiteral("Largest expense")};
    t.numericColumns = {1, 2, 3, 4, 5, 6, 7};
    for (int i = data.size() - 1; i >= 0; --i) {
        const auto &d = data[i];
        const Y *prev = i > 0 && data[i - 1].y == d.y - 1 ? &data[i - 1] : nullptr;
        t.addRow({Value::str(QString::number(d.y)), Value::money(d.t.income), prev ? change(d.t.income, prev->t.income) : Value::dash(),
                  Value::money(d.t.expense), prev ? change(d.t.expense, prev->t.expense) : Value::dash(), Value::money(d.t.net()),
                  d.t.income ? Value::percent(util::pct(d.t.net(), d.t.income)) : Value::dash(), Value::count(d.t.count), Value::str(d.top)});
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

namespace {

xlsx::Cell toCell(const Value &v, RowKind kind, bool firstColumn)
{
    xlsx::Cell c;
    switch (v.kind) {
    case Value::Kind::Text:
        // "—" means "not applicable": leave the cell empty so sums and filters work
        c = (v.text == QStringLiteral("—") || v.text == QStringLiteral("–")) ? xlsx::Cell::empty() : xlsx::Cell::str(v.text);
        break;
    case Value::Kind::Money: c = xlsx::Cell::money(v.cents, xlsx::Format::Money); break;
    case Value::Kind::MoneyDash: c = xlsx::Cell::money(v.cents, xlsx::Format::MoneyDash); break;
    case Value::Kind::Percent: c = xlsx::Cell::num(v.number / 100.0, xlsx::Format::Percent); break;
    case Value::Kind::Change: c = xlsx::Cell::num(v.number / 100.0, xlsx::Format::Change); break;
    case Value::Kind::Count: c = xlsx::Cell::num(v.number, xlsx::Format::Integer); break;
    case Value::Kind::Date: c = xlsx::Cell::day(v.date); break;
    default: c = xlsx::Cell::empty(); break;
    }
    switch (kind) {
    case RowKind::Section: c.bold = true; c.fill = xlsx::Fill::Section; break;
    case RowKind::Total: c.bold = true; c.fill = xlsx::Fill::Total; break;
    case RowKind::Net: c.bold = true; c.fill = xlsx::Fill::Net; break;
    default: break;
    }
    Q_UNUSED(firstColumn);
    return c;
}

xlsx::Cell header(const QString &text)
{
    xlsx::Cell c = xlsx::Cell::str(text);
    c.bold = true;
    c.fill = xlsx::Fill::Header;
    return c;
}

xlsx::Cell titleCell(const QString &text)
{
    xlsx::Cell c = xlsx::Cell::str(text);
    c.title = true;
    return c;
}

xlsx::Cell mutedCell(const QString &text)
{
    xlsx::Cell c = xlsx::Cell::str(text);
    c.muted = true;
    return c;
}

// Strip the emoji icon prefix from category labels for spreadsheets.
QString plainLabel(const QString &s)
{
    const int i = s.indexOf(QStringLiteral("  "));
    if (i > 0 && i <= 4) return s.mid(i + 2);
    return s;
}

} // namespace

QByteArray toXlsx(const Report &r, const QString &businessName)
{
    xlsx::Workbook wb(util::currencySymbol().trimmed(), util::currencyDecimals());
    const QString generated = QStringLiteral("Generated %1 by Ledgerly").arg(QLocale().toString(QDateTime::currentDateTime(), QStringLiteral("d MMM yyyy, HH:mm")));

    // Summary
    xlsx::Sheet &sum = wb.addSheet(QStringLiteral("Summary"));
    sum.addRow({titleCell(businessName)});
    sum.addRow({xlsx::Cell::str(QStringLiteral("%1 — %2").arg(r.title, r.period))});
    sum.addRow({mutedCell(generated)});
    sum.addBlankRow();
    if (!r.stats.isEmpty()) {
        xlsx::Cell valueHead = header(QStringLiteral("Value"));
        valueHead.align = xlsx::Align::Right;
        sum.addRow({header(QStringLiteral("Measure")), valueHead, header(QStringLiteral("Note"))});
        for (const Stat &st : r.stats) {
            xlsx::Cell label = xlsx::Cell::str(st.label);
            label.bold = true;
            sum.addRow({label, toCell(st.raw, RowKind::Normal, false), mutedCell(st.meta)});
        }
    }
    if (r.stats.isEmpty() && !r.tables.isEmpty()) sum.addRow({mutedCell(QStringLiteral("See the following sheets for details."))});
    sum.columnWidths = {26, 18, 30};

    // Chart data as a table (handy for building charts in Excel)
    if (r.chart.valid && !r.chart.labels.isEmpty()) {
        const bool byYear = r.kind == QLatin1String("yoy");
        xlsx::Sheet &sh = wb.addSheet(byYear ? QStringLiteral("Totals by year") : QStringLiteral("Monthly totals"));
        sh.addRow({titleCell(r.chart.title)});
        sh.addBlankRow();
        QVector<xlsx::Cell> th{header(byYear ? QStringLiteral("Year") : QStringLiteral("Month")), header(QStringLiteral("Income")),
                               header(QStringLiteral("Expenses")), header(QStringLiteral("Net profit"))};
        for (int c = 1; c < th.size(); ++c) th[c].align = xlsx::Align::Right;
        sh.addRow(th);
        sh.frozenRows = 3;
        qint64 ti = 0, te = 0;
        for (int i = 0; i < r.chart.labels.size(); ++i) {
            const qint64 in = r.chart.income.value(i), ex = r.chart.expense.value(i);
            ti += in;
            te += ex;
            xlsx::Cell label = byYear ? xlsx::Cell::num(r.chart.labels[i].toInt()) : xlsx::Cell::str(r.chart.fullLabels.value(i, r.chart.labels[i]));
            label.align = xlsx::Align::Left;
            sh.addRow({label, xlsx::Cell::money(in), xlsx::Cell::money(ex), xlsx::Cell::money(in - ex)});
        }
        if (!byYear) {
            QVector<xlsx::Cell> tot{xlsx::Cell::str(QStringLiteral("Total")), xlsx::Cell::money(ti), xlsx::Cell::money(te), xlsx::Cell::money(ti - te)};
            for (auto &c : tot) {
                c.bold = true;
                c.fill = xlsx::Fill::Total;
            }
            sh.addRow(tot);
        }
        sh.columnWidths = {18, 16, 16, 16};
    }

    // One sheet per table
    for (const Table &t : r.tables) {
        xlsx::Sheet &sh = wb.addSheet(t.title.section(QStringLiteral(" · "), 0, 0));
        sh.addRow({titleCell(t.title)});
        sh.addRow({mutedCell(t.subtitle.isEmpty() ? r.period : QStringLiteral("%1 · %2").arg(r.period, t.subtitle))});
        if (t.values.isEmpty()) {
            sh.addRow({mutedCell(t.emptyText)});
            sh.columnWidths = {30};
            continue;
        }
        QVector<xlsx::Cell> head;
        for (int c = 0; c < t.headers.size(); ++c) {
            xlsx::Cell hc = header(t.headers[c]);
            if (t.numericColumns.contains(c)) hc.align = xlsx::Align::Right;
            head << hc;
        }
        sh.addRow(head);
        sh.frozenRows = 3;
        for (int i = 0; i < t.values.size(); ++i) {
            const RowKind kind = t.kinds.value(i, RowKind::Normal);
            QVector<xlsx::Cell> cells;
            for (int c = 0; c < t.values[i].size(); ++c) {
                Value v = t.values[i][c];
                if (c == 0 && v.kind == Value::Kind::Text) v.text = plainLabel(v.text);
                cells << toCell(v, kind, c == 0);
            }
            sh.addRow(cells);
        }
        sh.autoSizeColumns(10, 40);
        if (!sh.columnWidths.isEmpty()) sh.columnWidths[0] = std::max(sh.columnWidths[0], 28.0);
    }
    return wb.toBytes();
}

bool saveXlsx(const Report &r, const QString &businessName, const QString &path, QString *error)
{
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        if (error) *error = f.errorString();
        return false;
    }
    f.write(toXlsx(r, businessName));
    if (!f.commit()) {
        if (error) *error = f.errorString();
        return false;
    }
    return true;
}

} // namespace reports
