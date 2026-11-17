#pragma once

#include "Models.h"
#include <QString>

class Store;

/* Backup files are JSON and use the same format as the Ledgerly web app,
 * so backups can move between the two. Receipts are embedded as data URLs. */
namespace backup {

QByteArray toJson(const Store &store, bool includeReceipts, int *receiptCount = nullptr);
bool parse(const QByteArray &json, BackupData *out, QString *error);

// Writes a backup file and records the time as the last backup.
bool writeFile(Store &store, const QString &path, bool includeReceipts, QString *error, int *receiptCount = nullptr);

// Writes a timestamped backup into dir and deletes the oldest ones beyond `keep`.
bool writeAutoBackup(Store &store, const QString &dir, int keep, QString *error, QString *writtenPath = nullptr);

QString defaultFileName(const Store &store, bool includeReceipts);
QString transactionsCsv(const Store &store, const QVector<Transaction> &list);
// Excel workbook with one filterable "Transactions" sheet (dates and amounts as real values).
QByteArray transactionsXlsx(const Store &store, const QVector<Transaction> &list, const QString &title = QString());
// Writes CSV or Excel depending on the file extension (.xlsx / .csv).
bool exportTransactions(const Store &store, const QVector<Transaction> &list, const QString &path, const QString &title, QString *error);

} // namespace backup
