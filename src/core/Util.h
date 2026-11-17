#pragma once

#include <QDate>
#include <QString>
#include <QStringList>
#include <QVector>

namespace util {

QString newId();
// Same input -> same id on every computer (used for default categories and recurring postings).
QString stableId(const QString &key);
QString nowIso();

// Currency formatting (amounts are integer cents).
void setCurrency(const QString &code);
QString currencyCode();
QString currencySymbol();
int currencyDecimals();
QString money(qint64 cents);
QString signedMoney(qint64 cents);   // "−$12.00" for negatives
QString moneyCompact(qint64 cents);  // "$12.5K"
QString amountPlain(qint64 cents);   // "1234.50" (for CSV / editing)

// Parses "1,234.50", "1234,50", "$12" etc. Returns -1 when invalid.
qint64 parseAmount(const QString &text);

QString monthName(int month, bool shortName = false);
QString monthLabel(int year, int month, bool shortName = false);
QString fmtDate(const QDate &d);
QString fmtDateLong(const QDate &d);
QString fmtDay(const QDate &d); // Today / Yesterday / Mon, 3 Nov 2026
QString fmtDateTime(const QString &iso);
QString fmtBytes(qint64 bytes);

double pct(qint64 part, qint64 whole);
QString pctText(qint64 part, qint64 whole);

QString toCsv(const QVector<QStringList> &rows);
QString slug(const QString &s);

} // namespace util
