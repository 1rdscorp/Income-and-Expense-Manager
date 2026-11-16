#pragma once

#include "Models.h"

#include <QHash>
#include <QObject>
#include <QSqlDatabase>
#include <array>
#include <functional>

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
    bool clearTables();
    void markDirty();

    QString m_dir;
    QSqlDatabase m_db;
    Settings m_settings;
    QVector<Category> m_categories;
    QHash<QString, int> m_catIndex;
    QVector<Transaction> m_transactions;
    QHash<QString, int> m_receiptCounts;
    qint64 m_receiptBytes = 0;
    bool m_dirty = false;
};
