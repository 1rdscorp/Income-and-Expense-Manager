#pragma once

#include <QDate>
#include <QString>
#include <QStringList>
#include <QVector>

class Store;

/* Report builders produce a display-independent model that the UI renders as
 * tables, and that is also exported to CSV and PDF/print. */
namespace reports {

enum class RowKind { Normal, Section, Total, Net };

/* A typed cell value: shown formatted in the app/PDF, exported as a real number to Excel. */
struct Value {
    enum class Kind { Empty, Text, Money, MoneyDash, Percent, Change, Count, Date };
    Kind kind = Kind::Empty;
    QString text;
    qint64 cents = 0;
    double number = 0.0; // Percent/Change: percentage points (47.3 = 47.3%)
    QDate date;

    static Value str(const QString &s) { Value v; v.kind = Kind::Text; v.text = s; return v; }
    static Value money(qint64 c) { Value v; v.kind = Kind::Money; v.cents = c; return v; }
    static Value moneyDash(qint64 c) { Value v; v.kind = Kind::MoneyDash; v.cents = c; return v; } // zero shown as "–"
    static Value percent(double p) { Value v; v.kind = Kind::Percent; v.number = p; return v; }
    static Value change(double p) { Value v; v.kind = Kind::Change; v.number = p; return v; }
    static Value count(qint64 n) { Value v; v.kind = Kind::Count; v.number = static_cast<double>(n); return v; }
    static Value day(const QDate &d) { Value v; v.kind = Kind::Date; v.date = d; return v; }
    static Value dash() { return str(QStringLiteral("—")); }
    static Value empty() { return Value(); }
};

QString display(const Value &v);

struct Table {
    QString title;
    QString subtitle;
    QStringList headers;
    QVector<int> numericColumns;
    QVector<QStringList> rows;          // display text (derived from values)
    QVector<QVector<Value>> values;     // typed cells
    QVector<RowKind> kinds;
    QString emptyText;
    bool half = false; // render side by side with the neighbouring half table

    void addRow(const QVector<Value> &cells, RowKind kind = RowKind::Normal);
};

struct Stat {
    QString label;
    QString value;
    QString meta;
    QString swatch; // "income", "expense" or empty
    bool negative = false;
    Value raw;
};

struct Chart {
    bool valid = false;
    QString title;
    QStringList labels;
    QStringList fullLabels;
    QVector<qint64> income;
    QVector<qint64> expense;
};

struct Report {
    QString kind; // monthly | annual | yoy
    QString title;
    QString period;
    QVector<Stat> stats;
    Chart chart;
    QVector<Table> tables;
    QVector<QStringList> csv;
};

Report monthly(const Store &store, int year, int month);
Report annual(const Store &store, int year);
Report yearOverYear(const Store &store);

// HTML for printing / PDF. If chartImageName is non-empty, an <img> referencing it is included.
QString toHtml(const Report &r, const QString &businessName, const QString &chartImageName = QString());

// Excel workbook: a Summary sheet, chart data (monthly/yearly totals) and one sheet per table.
QByteArray toXlsx(const Report &r, const QString &businessName);
bool saveXlsx(const Report &r, const QString &businessName, const QString &path, QString *error);

} // namespace reports
