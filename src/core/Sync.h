#pragma once

#include <QDateTime>
#include <QString>
#include <QStringList>

class Store;

/* Shared-folder sync for using Ledgerly on several computers via Google Drive,
 * OneDrive, Dropbox or a network share.
 *
 * Each computer keeps its own database. The shared folder only holds a change
 * journal: every computer writes *new* files under changes/<its id>/ and never
 * modifies a file, so cloud-sync tools never see two computers editing the same
 * file. Each computer applies the other computers' changes; when the same record
 * was changed in two places, the most recent change wins.
 *
 *   <folder>/ledgerly-shared.json        identifies the shared ledger
 *   <folder>/changes/<device>/<n>.json    change journal, one folder per computer
 *   <folder>/receipts/<id>                receipt files
 *   <folder>/devices/<device>.json        computer name and last sync time
 */
namespace sync {

struct FolderInfo {
    bool exists = false;
    bool hasLedger = false;   // already used as a shared folder
    QString ledgerId;
    QString createdBy;
    QStringList computers;    // names of computers that have synced
    QString error;
};

enum class JoinMode {
    Create,  // empty folder: upload this computer's data
    Replace, // existing shared data replaces this computer's data
    Merge,   // existing shared data + this computer's data combined
};

struct Result {
    bool ok = true;
    int exported = 0; // changes written to the folder
    int imported = 0; // changes applied from other computers
    QString error;
};

FolderInfo inspect(const QString &folder);
bool connect(Store &store, const QString &folder, JoinMode mode, QString *error);
void disconnect(Store &store);
bool isConnected(const Store &store);
QString folder(const Store &store);
QDateTime lastSync(const Store &store);
QString lastError(const Store &store); // empty when the last sync worked
QStringList computers(const Store &store); // other computers using the folder
QString computerName();

// Imports other computers' changes, records due recurring payments, writes our changes.
Result syncNow(Store &store);

} // namespace sync
