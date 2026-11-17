#pragma once

#include "BankImport.h"

#include <QDateTime>
#include <QString>
#include <QStringList>
#include <QVector>

class Store;

/* Live bank connection through SimpleFIN Bridge (https://bridge.simplefin.org),
 * an inexpensive service that connects to most U.S. banks and card issuers.
 *
 *   1. On SimpleFIN Bridge you connect your bank and create a "setup token".
 *   2. Ledgerly decodes the token (Base64 URL) and POSTs to it once to claim a
 *      private "access URL" (https://user:password@host/path).
 *   3. Ledgerly GETs <access URL>/accounts?start-date=…&end-date=… (Basic auth)
 *      and imports the transactions through the normal statement-import
 *      pipeline (categories, rules, duplicate detection).
 *
 * The access URL is a secret: it is stored only in this computer's database,
 * never in backups or the shared folder. */
namespace simplefin {

struct RemoteAccount {
    QString id;
    QString name;
    QString org;      // bank name
    QString currency;
    qint64 balance = 0;
    QVector<bank::StatementLine> lines; // posted transactions only
};

struct AccountSet {
    QVector<RemoteAccount> accounts;
    QStringList errors; // messages from the bridge (e.g. "re-authenticate with your bank")
};

// Low-level protocol (also used by tests).
bool parseAccountSet(const QByteArray &json, AccountSet *out, QString *error);
bool claim(const QString &setupToken, QString *accessUrl, QString *error, int timeoutMs = 30000);
bool fetch(const QString &accessUrl, const QDate &start, const QDate &end, bool balancesOnly, AccountSet *out, QString *error,
           int timeoutMs = 60000);

// Connection stored on this computer.
bool isConnected(const Store &store);
bool connect(Store &store, const QString &setupToken, QString *error); // claim, read accounts, link them
void disconnect(Store &store);
QDateTime lastDownload(const Store &store);
QString lastError(const Store &store);
QStringList linkedAccountNames(const Store &store);

struct DownloadResult {
    bool ok = true;
    int imported = 0;
    int skipped = 0; // already imported, possible duplicates, ignored by rules
    QStringList messages;
    QString error;
};
// Downloads new transactions for every linked account. Without force, skips if the
// last download was less than `minHours` ago (the bridge asks apps to poll sparingly).
DownloadResult download(Store &store, bool force = false, int minHours = 6);

} // namespace simplefin
