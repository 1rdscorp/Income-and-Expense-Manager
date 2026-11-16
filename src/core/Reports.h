#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

class Store;

/* Report builders produce a display-independent model that the UI renders as
 * tables, and that is also exported to CSV and PDF/print. */
namespace reports {

enum class RowKind { Normal, Section, Total, Net };

struct Table {
    QString title;
    QString subtitle;
    QStringList headers;
    QVector<int> numericColumns;
    QVector<QStringList> rows;
    QVector<RowKind> kinds;
    QString emptyText;
    bool half = false; // render side by side with the neighbouring half table
};

struct Stat {
    QString label;
    QString value;
    QString meta;
    QString swatch; // "income", "expense" or empty
    bool negative = false;
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

} // namespace reports
