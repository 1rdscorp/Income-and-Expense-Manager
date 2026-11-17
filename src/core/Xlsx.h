#pragma once

#include <QDate>
#include <QString>
#include <QVector>

/* Minimal, dependency-free writer for Excel .xlsx workbooks (Office Open XML).
 * Supports multiple sheets, text / number / date cells, number formats, bold,
 * fills, column widths, frozen header rows and auto-filters. */
namespace xlsx {

enum class Format {
    General,
    Integer,   // 1,234
    Money,     // currency, negatives in red
    MoneyDash, // currency, zero shown as "–"
    Percent,   // 47.3%
    Change,    // ▲ 12.3% / ▼ 4.0%
    Date,      // 17 Nov 2026
};

enum class Fill { None, Header, Section, Total, Net };
enum class Align { Auto, Left, Right, Center };

struct Cell {
    enum class Type { Empty, Text, Number, Date };
    Type type = Type::Empty;
    QString text;
    double number = 0.0;
    QDate date;
    Format format = Format::General;
    bool bold = false;
    bool italic = false;
    bool muted = false;
    bool title = false; // large bold heading
    Fill fill = Fill::None;
    Align align = Align::Auto;

    static Cell str(const QString &s) { Cell c; c.type = Type::Text; c.text = s; return c; }
    static Cell num(double v, Format f = Format::General) { Cell c; c.type = Type::Number; c.number = v; c.format = f; return c; }
    static Cell money(qint64 cents, Format f = Format::Money) { return num(static_cast<double>(cents) / 100.0, f); }
    static Cell day(const QDate &d) { Cell c; c.type = Type::Date; c.date = d; c.format = Format::Date; c.align = Align::Left; return c; }
    static Cell empty() { return Cell(); }
};

struct Sheet {
    QString name;
    QVector<QVector<Cell>> rows;
    QVector<double> columnWidths; // in characters; 0 = auto
    int frozenRows = 0;           // rows kept visible when scrolling
    int autoFilterRow = -1;       // 0-based header row for the auto-filter, -1 = none
    int autoFilterLastRow = -1;

    void addRow(const QVector<Cell> &cells) { rows.push_back(cells); }
    void addBlankRow() { rows.push_back({}); }
    void autoSizeColumns(double min = 8, double max = 60);
};

class Workbook {
public:
    // currencySymbol/decimals are used for Money formats (e.g. "$", 2).
    Workbook(const QString &currencySymbol = QStringLiteral("$"), int decimals = 2);
    Sheet &addSheet(const QString &name);
    QVector<Sheet> &sheets() { return m_sheets; }
    bool save(const QString &path, QString *error) const;
    QByteArray toBytes() const;

private:
    QString m_symbol;
    int m_decimals;
    QVector<Sheet> m_sheets;
};

// Excel sheet names: max 31 chars, no []:*?/\ ; made unique by the workbook.
QString sanitizeSheetName(const QString &name);
QString columnName(int index); // 0 -> A, 26 -> AA

} // namespace xlsx
