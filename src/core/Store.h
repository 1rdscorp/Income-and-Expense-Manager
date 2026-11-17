#pragma once

#include "Models.h"

#include <QHash>
#include <QJsonObject>
#include <QSet>
#include <QObject>
#include <QSqlDatabase>
#include <array>
#include <functional>
#include <utility>

/* Owns the SQLite database and an in-memory cache of categories and transactions.
 * Receipts' binary data stays in the database and is loaded on demand. */
class Store : public QObject
{
    Q_OBJECT
public:
    static Store &instance();

    bool open(const QString &dataDir, QString *error = nullptr);
    void close();
    QString dataDir() const { return m_dir; }
    QString databasePath() const;
    bool dirtySinceOpen() const { return m_dirty; }

    // Settings
    const Settings &settings() const { return m_settings; }
    bool saveSettings(const Settings &s);

    // Categories
    const QVector<Category> &categories() const { return m_categories; }
    QVector<Category> categoriesOfType(TxType t) const;
    const Category *category(const QString &id) const;
    Category categoryOrUnknown(const QString &id) const;
    bool categoryNameTaken(const QString &name, TxType type, const QString &exceptId) const;
    bool saveCategory(const Category &c, QString *error = nullptr);
    // moveTo empty => delete the category's transactions (and receipts)
    bool deleteCategory(const QString &id, const QString &moveTo, QString *error = nullptr);
    int seedDefaultCategories(bool all);

    // Transactions (sorted newest first)
    const QVector<Transaction> &transactions() const { return m_transactions; }
    const Transaction *transaction(const QString &id) const;
    bool saveTransaction(const Transaction &t, const QVector<Receipt> &newReceipts,
                         const QStringList &removedReceiptIds, QString *error = nullptr);
    bool deleteTransactions(const QStringList &ids, QString *error = nullptr);

    // Receipts
    int receiptCount(const QString &txId) const { return m_receiptCounts.value(txId, 0); }
    int totalReceipts() const;
    qint64 receiptBytes() const { return m_receiptBytes; }
    QVector<Receipt> receiptsFor(const QString &txId) const;
    QVector<Receipt> allReceipts() const;

    // Recurring transactions
    const QVector<Recurring> &recurring() const { return m_recurring; }
    const Recurring *recurringRule(const QString &id) const;
    int recurringPostedCount(const QString &id) const;
    // Creates or updates a rule, then records any payments already due.
    bool saveRecurring(const Recurring &r, QString *error = nullptr, const QDate &today = QDate::currentDate());
    bool setRecurringActive(const QString &id, bool active, const QDate &today = QDate::currentDate());
    // Turns an existing transaction into the first payment of a new recurring series.
    // recordMissed: also record occurrences between the transaction's date and today.
    bool makeRecurring(const QString &txId, Frequency frequency, bool recordMissed, QString *error = nullptr,
                       const QDate &today = QDate::currentDate(), QString *ruleId = nullptr);
    // Deletes the rule; payments already recorded are kept.
    bool deleteRecurring(const QString &id, QString *error = nullptr);
    // Records every occurrence due on or before `today`. Returns the transactions created.
    QVector<Transaction> processRecurring(const QDate &today = QDate::currentDate(), bool notify = true);
    // Occurrences of active rules between the dates (inclusive), soonest first.
    QVector<std::pair<QDate, Recurring>> upcoming(const QDate &from, const QDate &to) const;
    // Transactions recorded automatically this session (for the dashboard notice).
    const QVector<Transaction> &autoPosted() const { return m_autoPosted; }
    void clearAutoPosted() { m_autoPosted.clear(); }
    // Short messages for the dashboard (e.g. "12 transactions imported from Visa statement")
    const QStringList &notices() const { return m_notices; }
    void addNotice(const QString &text) { m_notices << text; emit dataChanged(); }
    void clearNotices() { m_notices.clear(); }

    // Bank accounts / credit cards and import categorisation rules
    const QVector<Account> &accounts() const { return m_accounts; }
    const Account *account(const QString &id) const;
    bool saveAccount(const Account &a, QString *error = nullptr, QString *savedId = nullptr);
    bool deleteAccount(const QString &id, QString *error = nullptr); // its transactions are kept
    int accountTransactionCount(const QString &accountId) const;
    const QVector<ImportRule> &importRules() const { return m_importRules; }
    bool saveImportRule(const ImportRule &r, QString *error = nullptr);
    bool deleteImportRule(const QString &id);
    // True if this statement line was imported before (even if since deleted on this computer).
    bool alreadyImported(const QString &accountId, const QString &externalId) const;
    // Adds imported statement lines in one go. Returns the number added.
    int importTransactions(const QVector<Transaction> &txs, QString *error = nullptr);

    // Small per-computer key/value settings (e.g. auto-import folders)
    QString localValue(const QString &key) const;
    void setLocalValue(const QString &key, const QString &value);

    // ---- Shared-folder sync support (see Sync.h) ----
    struct Change {
        QString entity;
        QString id;
        qint64 ts = 0;
    };
    enum class ApplyResult { Applied, Skipped, NotReady };
    QString deviceId() const { return m_deviceId; }
    bool isTracking() const { return m_tracking; }
    void setTracking(bool on) { m_tracking = on; }
    QVector<Change> pendingChanges(int limit = 2000) const;
    int pendingChangeCount() const;
    void clearPendingChanges(const QVector<Change> &done);
    void trackEverything(); // queue every record (first upload to a shared folder)
    QJsonObject recordJson(const QString &entity, const QString &id, bool *exists) const;
    QByteArray receiptData(const QString &id) const;
    // Applies another computer's change if it is newer than ours (last writer wins).
    // loadReceipt fetches a receipt's file; returning false means "not synced yet".
    ApplyResult applyRemote(const QString &entity, const QString &id, qint64 ts, const QString &device, bool deleted,
                            const QJsonObject &data, const std::function<bool(QByteArray *)> &loadReceipt);
    void beginRemoteBatch();
    void endRemoteBatch(bool changed);
    bool wipeForJoin(QString *error = nullptr); // empty everything locally without recording deletions
    QJsonObject syncConfig() const;
    void setSyncConfig(const QJsonObject &cfg);
    bool isFileApplied(const QString &name) const;
    void markFileApplied(const QString &name);

    // Bulk operations
    bool replaceAll(const BackupData &data, QString *error = nullptr);
    bool merge(const BackupData &data, MergeStats *stats, QString *error = nullptr);
    bool eraseAll(QString *error = nullptr);
    int loadSampleData();

    // Aggregations
    QVector<Transaction> filtered(const std::function<bool(const Transaction &)> &pred) const;
    QVector<Transaction> inMonth(int year, int month) const;
    QVector<Transaction> inYear(int year) const;
    QVector<Transaction> onDay(const QDate &d) const;
    static Totals totals(const QVector<Transaction> &txs);
    static QHash<QString, CategoryTotal> byCategory(const QVector<Transaction> &txs);
    std::array<Totals, 12> monthly(int year) const;
    QList<int> yearsWithData() const;
    QStringList knownVendors() const;
    bool backupIsStale() const;

signals:
    void dataChanged();
    void settingsChanged();

private:
    Store() = default;
    bool exec(const QString &sql, QString *error = nullptr);
    bool createSchema(QString *error);
    void loadCache();
    void sortTransactions();
    void sortCategories();
    bool writeCategory(const Category &c);
    bool writeTransaction(const Transaction &t);
    bool writeReceipt(const Receipt &r);
    bool writeRecurring(const Recurring &r);
    bool writeAccount(const Account &a);
    bool writeImportRule(const ImportRule &r);
    void trackChange(const QString &entity, const QString &id);
    void trackAllExisting();
    qint64 nextTs();
    bool addColumnIfMissing(const QString &table, const QString &column, const QString &type, QString *error);
    QStringList receiptIdsForTransactions(const QStringList &txIds) const;
    void sortRecurring();
    bool clearTables();
    void markDirty();

    QString m_dir;
    QSqlDatabase m_db;
    Settings m_settings;
    QVector<Category> m_categories;
    QHash<QString, int> m_catIndex;
    QVector<Transaction> m_transactions;
    QHash<QString, int> m_receiptCounts;
    QVector<Recurring> m_recurring;
    QVector<Account> m_accounts;
    QVector<ImportRule> m_importRules;
    QSet<QString> m_importedKeys; // accountId|externalId
    QString m_deviceId;
    bool m_tracking = false;
    bool m_applyingRemote = false;
    qint64 m_lastTs = 0;
    qint64 m_tsOverride = 0;
    QVector<Transaction> m_autoPosted;
    QStringList m_notices;
    qint64 m_receiptBytes = 0;
    bool m_dirty = false;
};
