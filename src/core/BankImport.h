#pragma once

#include "Models.h"

#include <QList>
#include <QStringList>

class Store;

/* Importing bank and credit-card statements (OFX/QFX, QIF, CSV): parsing,
 * automatic categorisation (rules, your past choices, keywords) and duplicate
 * detection. Used by the import dialog and by watched auto-import folders. */
namespace bank {

struct StatementLine {
    QDate date;
    qint64 amount = 0;   // signed cents: + money in, - money out
    QString description; // bank's text (payee / narrative)
    QString memo;
    QString externalId;  // bank transaction id (FITID) or a fingerprint
};

struct CsvMapping {
    QChar delimiter = QLatin1Char(',');
    bool hasHeader = true;
    int date = -1;
    int description = -1;
    int amount = -1;     // single signed amount column, or...
    int debit = -1;      // ...separate money-out / money-in columns
    int credit = -1;
    int memo = -1;
    int reference = -1;  // bank's own transaction reference (used to skip duplicates)
    QString dateFormat;  // Qt date format, e.g. "dd/MM/yyyy"
    bool isValid() const { return date >= 0 && description >= 0 && (amount >= 0 || debit >= 0 || credit >= 0) && !dateFormat.isEmpty(); }
    QString toJson() const;
    static CsvMapping fromJson(const QString &json);
};

struct ParseResult {
    QString format;              // "OFX", "QIF", "CSV"
    QVector<StatementLine> lines;
    bool looksLikeCard = false;  // OFX credit-card statement / QIF CCard
    QString accountHint;         // account number from the file, if any
    QList<QStringList> csvRows;  // raw rows (CSV only), for the column-mapping UI
    int csvHeaderRow = -1;       // index of the header row in csvRows, -1 if none
    CsvMapping csvMapping;       // detected or applied mapping (CSV only)
    QString error;
    bool ok() const { return error.isEmpty(); }
};

ParseResult parseFile(const QString &path, const CsvMapping *csv = nullptr);
ParseResult parseData(const QByteArray &data, const QString &fileName, const CsvMapping *csv = nullptr);
ParseResult parseOfx(const QByteArray &data);
ParseResult parseQif(const QByteArray &data);
// mapping: nullptr or invalid -> detect automatically
ParseResult parseCsv(const QByteArray &data, const CsvMapping *mapping = nullptr);

QList<QStringList> readCsv(const QByteArray &data, QChar *delimiter = nullptr);
CsvMapping guessCsvMapping(const QList<QStringList> &rows, QChar delimiter);
int findHeaderRow(const QList<QStringList> &rows); // first row naming a "date" column (banks often add info lines above)
QStringList dateFormats();
// preferMonthFirst: 0 = follow the system locale, 1 = month first (US, QIF), -1 = day first
QString detectDateFormat(const QStringList &samples, int preferMonthFirst = 0);
qint64 parseSignedAmount(const QString &text, bool *ok);

// "POS 4421 NETFLIX.COM 866-579-7172 CA" -> "Netflix.com"
QString cleanDescription(const QString &raw);
QString payeeKey(const QString &description);

struct Suggestion {
    QString categoryId;
    QString source; // "rule", "history", "keyword", "default"
    bool ignore = false;
    bool transfer = false; // looks like a card payment / transfer between own accounts
};
// True for lines like "AUTOPAY PAYMENT - THANK YOU" or "AMERICAN EXPRESS ACH PMT".
bool looksLikeTransfer(const QString &description);
Suggestion suggestCategory(const Store &store, const QString &description, TxType type);

struct Proposed {
    enum class Status { New, Duplicate, PossibleDuplicate, Ignored };
    StatementLine line;
    Transaction tx;      // ready to import (category chosen)
    Status status = Status::New;
    QString note;        // why it's flagged / where the category came from
    bool include = true; // default selection in the review list
};

// Turns statement lines into transactions for an account. flipSigns: the file shows money
// going out as positive numbers (e.g. American Express CSV) - swap income and expenses.
QVector<Proposed> prepare(const Store &store, const Account &account, const QVector<StatementLine> &lines, bool flipSigns = false);
// Whether the account's "swap signs" setting applies to this file format (CSV/QIF only;
// OFX/QFX/QBO and live connections always use the standard sign convention).
bool flipApplies(const Account &account, const QString &format);
// Card statement where most amounts are positive -> charges are probably shown as positive.
bool chargesLookPositive(const Account &account, const QVector<StatementLine> &lines);
int commit(Store &store, const QVector<Proposed> &items, QString *error); // imports included items

struct AutoImportResult {
    QString file;
    int imported = 0;
    int skipped = 0;
    QString error;
};
// Imports every statement file waiting in the account's watched folder and moves it to "Imported".
QVector<AutoImportResult> runAutoImport(Store &store);
QString watchFolder(const Store &store, const QString &accountId);
void setWatchFolder(Store &store, const QString &accountId, const QString &folder);

} // namespace bank
