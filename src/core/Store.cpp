#include "Store.h"
#include "Defaults.h"
#include "Json.h"
#include "Recurrence.h"
#include "Util.h"

#include <QDateTime>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <QTimeZone>
#include <QVariant>
#include <algorithm>
#include <random>
#include <cmath>

namespace {
const char *kConn = "ledgerly";

QJsonObject settingsToJson(const Settings &s)
{
    QJsonObject o;
    o[QStringLiteral("businessName")] = s.businessName;
    o[QStringLiteral("currency")] = s.currency;
    o[QStringLiteral("theme")] = s.theme;
    o[QStringLiteral("compressReceipts")] = s.compressReceipts;
    o[QStringLiteral("backupReminderDays")] = s.backupReminderDays;
    o[QStringLiteral("lastBackupAt")] = s.lastBackupAt;
    o[QStringLiteral("autoBackupDir")] = s.autoBackupDir;
    o[QStringLiteral("autoBackupKeep")] = s.autoBackupKeep;
    return o;
}

Settings settingsFromJson(const QJsonObject &o)
{
    Settings s;
    if (o.contains(QStringLiteral("businessName"))) s.businessName = o.value(QStringLiteral("businessName")).toString(s.businessName);
    if (o.contains(QStringLiteral("currency"))) s.currency = o.value(QStringLiteral("currency")).toString(s.currency);
    if (o.contains(QStringLiteral("theme"))) s.theme = o.value(QStringLiteral("theme")).toString(s.theme);
    if (o.contains(QStringLiteral("compressReceipts"))) s.compressReceipts = o.value(QStringLiteral("compressReceipts")).toBool(true);
    if (o.contains(QStringLiteral("backupReminderDays"))) s.backupReminderDays = o.value(QStringLiteral("backupReminderDays")).toInt(7);
    s.lastBackupAt = o.value(QStringLiteral("lastBackupAt")).toString();
    s.autoBackupDir = o.value(QStringLiteral("autoBackupDir")).toString();
    if (o.contains(QStringLiteral("autoBackupKeep"))) s.autoBackupKeep = o.value(QStringLiteral("autoBackupKeep")).toInt(10);
    return s;
}
} // namespace

Store &Store::instance()
{
    static Store s;
    return s;
}

QString Store::databasePath() const { return QDir(m_dir).filePath(QStringLiteral("ledgerly.db")); }

bool Store::exec(const QString &sql, QString *error)
{
    QSqlQuery q(m_db);
    if (!q.exec(sql)) {
        if (error) *error = q.lastError().text();
        return false;
    }
    return true;
}

bool Store::open(const QString &dataDir, QString *error)
{
    close();
    m_dir = dataDir;
    if (!QDir().mkpath(dataDir)) {
        if (error) *error = QStringLiteral("Cannot create data folder: %1").arg(dataDir);
        return false;
    }
    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QString::fromLatin1(kConn));
    m_db.setDatabaseName(databasePath());
    if (!m_db.open()) {
        if (error) *error = m_db.lastError().text();
        return false;
    }
    exec(QStringLiteral("PRAGMA foreign_keys = OFF"));
    if (!createSchema(error)) return false;

    m_deviceId = localValue(QStringLiteral("device_id"));
    if (m_deviceId.isEmpty()) {
        m_deviceId = util::newId();
        setLocalValue(QStringLiteral("device_id"), m_deviceId);
    }
    m_tracking = !syncConfig().value(QStringLiteral("folder")).toString().isEmpty();

    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT value FROM meta WHERE key = 'settings'"));
    if (q.exec() && q.next())
        m_settings = settingsFromJson(QJsonDocument::fromJson(q.value(0).toString().toUtf8()).object());
    else
        m_settings = Settings();
    util::setCurrency(m_settings.currency);

    loadCache();

    QSqlQuery init(m_db);
    init.exec(QStringLiteral("SELECT value FROM meta WHERE key = 'initialized'"));
    if (!init.next()) {
        if (m_categories.isEmpty()) seedDefaultCategories(true);
        exec(QStringLiteral("INSERT OR REPLACE INTO meta(key, value) VALUES('initialized', '1')"));
        saveSettings(m_settings);
    }
    m_dirty = false;
    return true;
}

void Store::close()
{
    if (m_db.isValid()) {
        m_db.close();
        m_db = QSqlDatabase();
        QSqlDatabase::removeDatabase(QString::fromLatin1(kConn));
    }
    m_categories.clear();
    m_transactions.clear();
    m_receiptCounts.clear();
    m_catIndex.clear();
}

bool Store::createSchema(QString *error)
{
    const char *stmts[] = {
        "CREATE TABLE IF NOT EXISTS meta (key TEXT PRIMARY KEY, value TEXT)",
        "CREATE TABLE IF NOT EXISTS categories (id TEXT PRIMARY KEY, name TEXT NOT NULL, type TEXT NOT NULL,"
        " icon TEXT, budget INTEGER NOT NULL DEFAULT 0, created_at TEXT)",
        "CREATE TABLE IF NOT EXISTS transactions (id TEXT PRIMARY KEY, type TEXT NOT NULL, category_id TEXT,"
        " amount INTEGER NOT NULL, date TEXT NOT NULL, description TEXT, vendor TEXT, payment_method TEXT,"
        " notes TEXT, created_at TEXT, updated_at TEXT)",
        "CREATE INDEX IF NOT EXISTS idx_tx_date ON transactions(date)",
        "CREATE INDEX IF NOT EXISTS idx_tx_cat ON transactions(category_id)",
        "CREATE TABLE IF NOT EXISTS receipts (id TEXT PRIMARY KEY, transaction_id TEXT NOT NULL, name TEXT,"
        " mime TEXT, size INTEGER, data BLOB, created_at TEXT)",
        "CREATE INDEX IF NOT EXISTS idx_receipt_tx ON receipts(transaction_id)",
        "CREATE TABLE IF NOT EXISTS recurring (id TEXT PRIMARY KEY, type TEXT NOT NULL, category_id TEXT,"
        " amount INTEGER NOT NULL, description TEXT, vendor TEXT, payment_method TEXT, notes TEXT,"
        " frequency TEXT NOT NULL, day INTEGER NOT NULL, start_date TEXT NOT NULL, end_date TEXT,"
        " next_date TEXT, active INTEGER NOT NULL DEFAULT 1, created_at TEXT, updated_at TEXT)",
    };
    for (const char *s : stmts)
        if (!exec(QString::fromLatin1(s), error)) return false;

    // Older databases: add newer columns in place
    if (!addColumnIfMissing(QStringLiteral("transactions"), QStringLiteral("recurring_id"), QStringLiteral("TEXT"), error)) return false; // v1.1
    if (!addColumnIfMissing(QStringLiteral("transactions"), QStringLiteral("account_id"), QStringLiteral("TEXT"), error)) return false;   // v1.3
    if (!addColumnIfMissing(QStringLiteral("transactions"), QStringLiteral("external_id"), QStringLiteral("TEXT"), error)) return false;  // v1.3
    const char *more[] = {
        "CREATE INDEX IF NOT EXISTS idx_tx_recurring ON transactions(recurring_id)",
        "CREATE INDEX IF NOT EXISTS idx_tx_external ON transactions(account_id, external_id)",
        "CREATE TABLE IF NOT EXISTS accounts (id TEXT PRIMARY KEY, name TEXT NOT NULL, kind TEXT, flip_signs INTEGER NOT NULL DEFAULT 0,"
        " csv_mapping TEXT, created_at TEXT, updated_at TEXT)",
        "CREATE TABLE IF NOT EXISTS import_rules (id TEXT PRIMARY KEY, pattern TEXT NOT NULL, category_id TEXT,"
        " ignore INTEGER NOT NULL DEFAULT 0, created_at TEXT, updated_at TEXT)",
        // statement lines imported on this computer (kept even if the transaction is deleted later)
        "CREATE TABLE IF NOT EXISTS import_seen (account_id TEXT NOT NULL, external_id TEXT NOT NULL, PRIMARY KEY(account_id, external_id))",
        // shared-folder sync: local changes waiting to be written, last change time per record, journal files applied
        "CREATE TABLE IF NOT EXISTS sync_outbox (entity TEXT NOT NULL, id TEXT NOT NULL, ts INTEGER NOT NULL, PRIMARY KEY(entity, id))",
        "CREATE TABLE IF NOT EXISTS sync_versions (entity TEXT NOT NULL, id TEXT NOT NULL, ts INTEGER NOT NULL, device TEXT, PRIMARY KEY(entity, id))",
        "CREATE TABLE IF NOT EXISTS sync_files (name TEXT PRIMARY KEY)",
        "CREATE TABLE IF NOT EXISTS local (key TEXT PRIMARY KEY, value TEXT)",
    };
    for (const char *m : more)
        if (!exec(QString::fromLatin1(m), error)) return false;
    return true;
}

void Store::loadCache()
{
    m_categories.clear();
    m_transactions.clear();
    m_receiptCounts.clear();
    m_recurring.clear();
    m_accounts.clear();
    m_importRules.clear();
    m_importedKeys.clear();
    m_receiptBytes = 0;

    QSqlQuery q(m_db);
    q.exec(QStringLiteral("SELECT id, name, type, icon, budget, created_at FROM categories"));
    while (q.next()) {
        Category c;
        c.id = q.value(0).toString();
        c.name = q.value(1).toString();
        c.type = typeFromString(q.value(2).toString());
        c.icon = q.value(3).toString();
        c.budget = q.value(4).toLongLong();
        c.createdAt = q.value(5).toString();
        m_categories.push_back(c);
    }
    sortCategories();

    q.exec(QStringLiteral("SELECT id, type, category_id, amount, date, description, vendor, payment_method, notes,"
                          " created_at, updated_at, recurring_id, account_id, external_id FROM transactions"));
    while (q.next()) {
        Transaction t;
        t.id = q.value(0).toString();
        t.type = typeFromString(q.value(1).toString());
        t.categoryId = q.value(2).toString();
        t.amount = q.value(3).toLongLong();
        t.date = QDate::fromString(q.value(4).toString(), Qt::ISODate);
        t.description = q.value(5).toString();
        t.vendor = q.value(6).toString();
        t.paymentMethod = q.value(7).toString();
        t.notes = q.value(8).toString();
        t.createdAt = q.value(9).toString();
        t.updatedAt = q.value(10).toString();
        t.recurringId = q.value(11).toString();
        t.accountId = q.value(12).toString();
        t.externalId = q.value(13).toString();
        if (!t.externalId.isEmpty()) m_importedKeys.insert(t.accountId + QLatin1Char('|') + t.externalId);
        m_transactions.push_back(t);
    }
    sortTransactions();

    q.exec(QStringLiteral("SELECT id, type, category_id, amount, description, vendor, payment_method, notes, frequency, day,"
                          " start_date, end_date, next_date, active, created_at, updated_at FROM recurring"));
    while (q.next()) {
        Recurring r;
        r.id = q.value(0).toString();
        r.type = typeFromString(q.value(1).toString());
        r.categoryId = q.value(2).toString();
        r.amount = q.value(3).toLongLong();
        r.description = q.value(4).toString();
        r.vendor = q.value(5).toString();
        r.paymentMethod = q.value(6).toString();
        r.notes = q.value(7).toString();
        r.frequency = frequencyFromString(q.value(8).toString());
        r.day = q.value(9).toInt();
        r.startDate = QDate::fromString(q.value(10).toString(), Qt::ISODate);
        r.endDate = QDate::fromString(q.value(11).toString(), Qt::ISODate);
        r.nextDate = QDate::fromString(q.value(12).toString(), Qt::ISODate);
        r.active = q.value(13).toInt() != 0;
        r.createdAt = q.value(14).toString();
        r.updatedAt = q.value(15).toString();
        m_recurring.push_back(r);
    }
    sortRecurring();

    q.exec(QStringLiteral("SELECT id, name, kind, flip_signs, csv_mapping, created_at, updated_at FROM accounts"));
    while (q.next()) {
        Account a;
        a.id = q.value(0).toString();
        a.name = q.value(1).toString();
        a.kind = q.value(2).toString() == QLatin1String("card") ? QStringLiteral("card") : QStringLiteral("bank");
        a.flipSigns = q.value(3).toInt() != 0;
        a.csvMapping = q.value(4).toString();
        a.createdAt = q.value(5).toString();
        a.updatedAt = q.value(6).toString();
        m_accounts.push_back(a);
    }
    std::sort(m_accounts.begin(), m_accounts.end(), [](const Account &a, const Account &b) { return QString::localeAwareCompare(a.name, b.name) < 0; });
    q.exec(QStringLiteral("SELECT id, pattern, category_id, ignore, created_at, updated_at FROM import_rules"));
    while (q.next()) {
        ImportRule r;
        r.id = q.value(0).toString();
        r.pattern = q.value(1).toString();
        r.categoryId = q.value(2).toString();
        r.ignore = q.value(3).toInt() != 0;
        r.createdAt = q.value(4).toString();
        r.updatedAt = q.value(5).toString();
        m_importRules.push_back(r);
    }
    std::sort(m_importRules.begin(), m_importRules.end(), [](const ImportRule &a, const ImportRule &b) { return a.pattern.compare(b.pattern, Qt::CaseInsensitive) < 0; });
    q.exec(QStringLiteral("SELECT account_id, external_id FROM import_seen"));
    while (q.next()) m_importedKeys.insert(q.value(0).toString() + QLatin1Char('|') + q.value(1).toString());

    q.exec(QStringLiteral("SELECT transaction_id, COUNT(*), SUM(size) FROM receipts GROUP BY transaction_id"));
    while (q.next()) {
        m_receiptCounts.insert(q.value(0).toString(), q.value(1).toInt());
        m_receiptBytes += q.value(2).toLongLong();
    }
}

void Store::sortTransactions()
{
    std::stable_sort(m_transactions.begin(), m_transactions.end(), [](const Transaction &a, const Transaction &b) {
        if (a.date != b.date) return a.date > b.date;
        return a.createdAt > b.createdAt;
    });
}

void Store::sortCategories()
{
    std::sort(m_categories.begin(), m_categories.end(), [](const Category &a, const Category &b) {
        return QString::localeAwareCompare(a.name, b.name) < 0;
    });
    m_catIndex.clear();
    for (int i = 0; i < m_categories.size(); ++i) m_catIndex.insert(m_categories[i].id, i);
}

void Store::markDirty() { m_dirty = true; }

/* ---------------- Settings ---------------- */

bool Store::saveSettings(const Settings &s)
{
    const bool shared = s.businessName != m_settings.businessName || s.currency != m_settings.currency;
    m_settings = s;
    if (shared) trackChange(QStringLiteral("settings"), QStringLiteral("business"));
    util::setCurrency(s.currency);
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("INSERT OR REPLACE INTO meta(key, value) VALUES('settings', ?)"));
    q.addBindValue(QString::fromUtf8(QJsonDocument(settingsToJson(s)).toJson(QJsonDocument::Compact)));
    const bool ok = q.exec();
    emit settingsChanged();
    return ok;
}

/* ---------------- Categories ---------------- */

QVector<Category> Store::categoriesOfType(TxType t) const
{
    QVector<Category> out;
    for (const auto &c : m_categories)
        if (c.type == t) out.push_back(c);
    return out;
}

const Category *Store::category(const QString &id) const
{
    auto it = m_catIndex.constFind(id);
    return it == m_catIndex.constEnd() ? nullptr : &m_categories[*it];
}

Category Store::categoryOrUnknown(const QString &id) const
{
    if (const Category *c = category(id)) return *c;
    Category c;
    c.id = id;
    c.name = QStringLiteral("Uncategorized");
    c.icon = QStringLiteral("?");
    return c;
}

bool Store::categoryNameTaken(const QString &name, TxType type, const QString &exceptId) const
{
    for (const auto &c : m_categories)
        if (c.id != exceptId && c.type == type && c.name.compare(name, Qt::CaseInsensitive) == 0) return true;
    return false;
}

bool Store::writeCategory(const Category &c)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("INSERT OR REPLACE INTO categories(id, name, type, icon, budget, created_at) VALUES(?,?,?,?,?,?)"));
    q.addBindValue(c.id);
    q.addBindValue(c.name);
    q.addBindValue(typeToString(c.type));
    q.addBindValue(c.icon);
    q.addBindValue(c.budget);
    q.addBindValue(c.createdAt);
    const bool ok = q.exec();
    if (ok) trackChange(QStringLiteral("category"), c.id);
    return ok;
}

bool Store::saveCategory(const Category &input, QString *error)
{
    Category c = input;
    c.name = c.name.simplified();
    if (c.name.isEmpty()) {
        if (error) *error = QStringLiteral("Enter a category name.");
        return false;
    }
    if (categoryNameTaken(c.name, c.type, c.id)) {
        if (error) *error = QStringLiteral("A %1 category named \"%2\" already exists.").arg(typeToString(c.type), c.name);
        return false;
    }
    if (c.id.isEmpty()) c.id = util::newId();
    if (c.createdAt.isEmpty()) c.createdAt = util::nowIso();
    if (c.type == TxType::Income) c.budget = 0;
    if (c.icon.isEmpty()) c.icon = QStringLiteral("🏷");

    const Category *old = category(c.id);
    const bool typeChanged = old && old->type != c.type;

    m_db.transaction();
    bool ok = writeCategory(c);
    if (ok && typeChanged) {
        QSqlQuery q(m_db);
        q.prepare(QStringLiteral("UPDATE transactions SET type = ?, updated_at = ? WHERE category_id = ?"));
        q.addBindValue(typeToString(c.type));
        q.addBindValue(util::nowIso());
        q.addBindValue(c.id);
        ok = q.exec();
        if (ok) {
            q.prepare(QStringLiteral("UPDATE recurring SET type = ? WHERE category_id = ?"));
            q.addBindValue(typeToString(c.type));
            q.addBindValue(c.id);
            ok = q.exec();
        }
    }
    if (!ok || !m_db.commit()) {
        if (error) *error = m_db.lastError().text();
        m_db.rollback();
        return false;
    }
    if (typeChanged) {
        for (auto &r : m_recurring)
            if (r.categoryId == c.id) {
                r.type = c.type;
                trackChange(QStringLiteral("recurring"), r.id);
            }
        for (const auto &t : m_transactions)
            if (t.categoryId == c.id) trackChange(QStringLiteral("transaction"), t.id);
    }
    if (old) m_categories[m_catIndex.value(c.id)] = c;
    else m_categories.push_back(c);
    if (typeChanged)
        for (auto &t : m_transactions)
            if (t.categoryId == c.id) t.type = c.type;
    sortCategories();
    markDirty();
    emit dataChanged();
    return true;
}

bool Store::deleteCategory(const QString &id, const QString &moveTo, QString *error)
{
    QStringList txIds, ruleIds;
    for (const auto &t : m_transactions)
        if (t.categoryId == id) txIds << t.id;
    for (const auto &r : m_recurring)
        if (r.categoryId == id) ruleIds << r.id;
    const QStringList receiptIds = moveTo.isEmpty() ? receiptIdsForTransactions(txIds) : QStringList();

    if (!moveTo.isEmpty() && (moveTo == id || !category(moveTo))) {
        if (error) *error = QStringLiteral("Choose a valid category to move transactions to.");
        return false;
    }

    m_db.transaction();
    bool ok = true;
    QSqlQuery q(m_db);
    if (!txIds.isEmpty()) {
        if (!moveTo.isEmpty()) {
            q.prepare(QStringLiteral("UPDATE transactions SET category_id = ?, type = ?, updated_at = ? WHERE category_id = ?"));
            q.addBindValue(moveTo);
            q.addBindValue(typeToString(category(moveTo)->type));
            q.addBindValue(util::nowIso());
            q.addBindValue(id);
            ok = q.exec();
        } else {
            q.prepare(QStringLiteral("DELETE FROM receipts WHERE transaction_id IN (SELECT id FROM transactions WHERE category_id = ?)"));
            q.addBindValue(id);
            ok = q.exec();
            if (ok) {
                q.prepare(QStringLiteral("DELETE FROM transactions WHERE category_id = ?"));
                q.addBindValue(id);
                ok = q.exec();
            }
        }
    }
    if (ok) {
        if (!moveTo.isEmpty()) {
            q.prepare(QStringLiteral("UPDATE recurring SET category_id = ?, type = ? WHERE category_id = ?"));
            q.addBindValue(moveTo);
            q.addBindValue(typeToString(category(moveTo)->type));
            q.addBindValue(id);
        } else {
            q.prepare(QStringLiteral("DELETE FROM recurring WHERE category_id = ?"));
            q.addBindValue(id);
        }
        ok = q.exec();
    }
    if (ok) {
        q.prepare(QStringLiteral("DELETE FROM categories WHERE id = ?"));
        q.addBindValue(id);
        ok = q.exec();
    }
    if (ok) {
        trackChange(QStringLiteral("category"), id);
        for (const auto &x : txIds) trackChange(QStringLiteral("transaction"), x);
        for (const auto &x : ruleIds) trackChange(QStringLiteral("recurring"), x);
        for (const auto &x : receiptIds) trackChange(QStringLiteral("receipt"), x);
    }
    if (!ok || !m_db.commit()) {
        if (error) *error = q.lastError().text();
        m_db.rollback();
        return false;
    }
    loadCache();
    markDirty();
    emit dataChanged();
    return true;
}

int Store::seedDefaultCategories(bool all)
{
    QSet<QString> have;
    for (const auto &c : m_categories) have.insert(typeToString(c.type) + QLatin1Char('|') + c.name.toLower());
    int added = 0;
    m_db.transaction();
    for (const auto &d : defaults::categories()) {
        if (!all && have.contains(typeToString(d.type) + QLatin1Char('|') + d.name.toLower())) continue;
        Category c;
        // Stable ids: two computers sharing a folder get the same default categories.
        c.id = util::stableId(QStringLiteral("default-category|%1|%2").arg(typeToString(d.type), d.name));
        if (category(c.id)) c.id = util::newId();
        c.name = d.name;
        c.type = d.type;
        c.icon = d.icon;
        c.createdAt = util::nowIso();
        if (writeCategory(c)) {
            m_categories.push_back(c);
            ++added;
        }
    }
    m_db.commit();
    sortCategories();
    if (added) {
        markDirty();
        emit dataChanged();
    }
    return added;
}

/* ---------------- Transactions ---------------- */

const Transaction *Store::transaction(const QString &id) const
{
    for (const auto &t : m_transactions)
        if (t.id == id) return &t;
    return nullptr;
}

bool Store::writeTransaction(const Transaction &t)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("INSERT OR REPLACE INTO transactions(id, type, category_id, amount, date, description, vendor,"
                             " payment_method, notes, created_at, updated_at, recurring_id, account_id, external_id)"
                             " VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?)"));
    q.addBindValue(t.id);
    q.addBindValue(typeToString(t.type));
    q.addBindValue(t.categoryId);
    q.addBindValue(t.amount);
    q.addBindValue(t.date.toString(Qt::ISODate));
    q.addBindValue(t.description);
    q.addBindValue(t.vendor);
    q.addBindValue(t.paymentMethod);
    q.addBindValue(t.notes);
    q.addBindValue(t.createdAt);
    q.addBindValue(t.updatedAt);
    const QVariant null{QMetaType(QMetaType::QString)};
    q.addBindValue(t.recurringId.isEmpty() ? null : QVariant(t.recurringId));
    q.addBindValue(t.accountId.isEmpty() ? null : QVariant(t.accountId));
    q.addBindValue(t.externalId.isEmpty() ? null : QVariant(t.externalId));
    const bool ok = q.exec();
    if (ok) {
        trackChange(QStringLiteral("transaction"), t.id);
        if (!t.externalId.isEmpty()) m_importedKeys.insert(t.accountId + QLatin1Char('|') + t.externalId);
    }
    return ok;
}

bool Store::writeRecurring(const Recurring &r)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("INSERT OR REPLACE INTO recurring(id, type, category_id, amount, description, vendor, payment_method, notes,"
                             " frequency, day, start_date, end_date, next_date, active, created_at, updated_at)"
                             " VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)"));
    q.addBindValue(r.id);
    q.addBindValue(typeToString(r.type));
    q.addBindValue(r.categoryId);
    q.addBindValue(r.amount);
    q.addBindValue(r.description);
    q.addBindValue(r.vendor);
    q.addBindValue(r.paymentMethod);
    q.addBindValue(r.notes);
    q.addBindValue(frequencyToString(r.frequency));
    q.addBindValue(r.day);
    q.addBindValue(r.startDate.toString(Qt::ISODate));
    q.addBindValue(r.endDate.isValid() ? r.endDate.toString(Qt::ISODate) : QString());
    q.addBindValue(r.nextDate.isValid() ? r.nextDate.toString(Qt::ISODate) : QString());
    q.addBindValue(r.active ? 1 : 0);
    q.addBindValue(r.createdAt);
    q.addBindValue(r.updatedAt);
    const bool ok = q.exec();
    if (ok) trackChange(QStringLiteral("recurring"), r.id);
    return ok;
}

bool Store::writeReceipt(const Receipt &r)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("INSERT OR REPLACE INTO receipts(id, transaction_id, name, mime, size, data, created_at) VALUES(?,?,?,?,?,?,?)"));
    q.addBindValue(r.id);
    q.addBindValue(r.transactionId);
    q.addBindValue(r.name);
    q.addBindValue(r.mime);
    q.addBindValue(static_cast<qint64>(r.data.size()));
    q.addBindValue(r.data);
    q.addBindValue(r.createdAt);
    const bool ok = q.exec();
    if (ok) trackChange(QStringLiteral("receipt"), r.id);
    return ok;
}

bool Store::saveTransaction(const Transaction &input, const QVector<Receipt> &newReceipts,
                            const QStringList &removedReceiptIds, QString *error)
{
    Transaction t = input;
    if (t.amount <= 0) {
        if (error) *error = QStringLiteral("Enter an amount greater than zero.");
        return false;
    }
    if (!t.date.isValid()) {
        if (error) *error = QStringLiteral("Choose a valid date.");
        return false;
    }
    if (!category(t.categoryId)) {
        if (error) *error = QStringLiteral("Choose a category.");
        return false;
    }
    t.type = category(t.categoryId)->type;
    const QString now = util::nowIso();
    if (t.id.isEmpty()) t.id = util::newId();
    if (t.createdAt.isEmpty()) t.createdAt = now;
    t.updatedAt = now;

    m_db.transaction();
    bool ok = writeTransaction(t);
    for (Receipt r : newReceipts) {
        if (!ok) break;
        r.transactionId = t.id;
        if (r.id.isEmpty()) r.id = util::newId();
        if (r.createdAt.isEmpty()) r.createdAt = now;
        ok = writeReceipt(r);
    }
    for (const QString &rid : removedReceiptIds) {
        if (!ok) break;
        QSqlQuery q(m_db);
        q.prepare(QStringLiteral("DELETE FROM receipts WHERE id = ? AND transaction_id = ?"));
        q.addBindValue(rid);
        q.addBindValue(t.id);
        ok = q.exec();
        if (ok) trackChange(QStringLiteral("receipt"), rid);
    }
    if (!ok || !m_db.commit()) {
        if (error) *error = QStringLiteral("Could not save: %1").arg(m_db.lastError().text());
        m_db.rollback();
        return false;
    }

    bool found = false;
    for (auto &x : m_transactions) {
        if (x.id == t.id) {
            x = t;
            found = true;
            break;
        }
    }
    if (!found) m_transactions.push_back(t);
    sortTransactions();

    // refresh receipt stats for this transaction
    QSqlQuery q(m_db);
    q.exec(QStringLiteral("SELECT COUNT(*), COALESCE(SUM(size),0) FROM receipts"));
    if (q.next()) m_receiptBytes = q.value(1).toLongLong();
    q.prepare(QStringLiteral("SELECT COUNT(*) FROM receipts WHERE transaction_id = ?"));
    q.addBindValue(t.id);
    if (q.exec() && q.next()) {
        const int n = q.value(0).toInt();
        if (n) m_receiptCounts.insert(t.id, n);
        else m_receiptCounts.remove(t.id);
    }
    markDirty();
    emit dataChanged();
    return true;
}

bool Store::deleteTransactions(const QStringList &ids, QString *error)
{
    if (ids.isEmpty()) return true;
    const QStringList receiptIds = receiptIdsForTransactions(ids);
    m_db.transaction();
    bool ok = true;
    for (const QString &rid : receiptIds) trackChange(QStringLiteral("receipt"), rid);
    for (const QString &id : ids) {
        trackChange(QStringLiteral("transaction"), id);
        QSqlQuery q(m_db);
        q.prepare(QStringLiteral("DELETE FROM receipts WHERE transaction_id = ?"));
        q.addBindValue(id);
        ok = ok && q.exec();
        q.prepare(QStringLiteral("DELETE FROM transactions WHERE id = ?"));
        q.addBindValue(id);
        ok = ok && q.exec();
    }
    if (!ok || !m_db.commit()) {
        if (error) *error = m_db.lastError().text();
        m_db.rollback();
        return false;
    }
    loadCache();
    markDirty();
    emit dataChanged();
    return true;
}

/* ---------------- Receipts ---------------- */

int Store::totalReceipts() const
{
    int n = 0;
    for (int v : m_receiptCounts) n += v;
    return n;
}

static Receipt receiptFromQuery(const QSqlQuery &q)
{
    Receipt r;
    r.id = q.value(0).toString();
    r.transactionId = q.value(1).toString();
    r.name = q.value(2).toString();
    r.mime = q.value(3).toString();
    r.size = q.value(4).toLongLong();
    r.data = q.value(5).toByteArray();
    r.createdAt = q.value(6).toString();
    return r;
}

QVector<Receipt> Store::receiptsFor(const QString &txId) const
{
    QVector<Receipt> out;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT id, transaction_id, name, mime, size, data, created_at FROM receipts WHERE transaction_id = ? ORDER BY created_at"));
    q.addBindValue(txId);
    if (q.exec())
        while (q.next()) out.push_back(receiptFromQuery(q));
    return out;
}

QVector<Receipt> Store::allReceipts() const
{
    QVector<Receipt> out;
    QSqlQuery q(m_db);
    if (q.exec(QStringLiteral("SELECT id, transaction_id, name, mime, size, data, created_at FROM receipts")))
        while (q.next()) out.push_back(receiptFromQuery(q));
    return out;
}

/* ---------------- Bulk ---------------- */

bool Store::clearTables()
{
    trackAllExisting(); // deletions reach other computers sharing the folder
    return exec(QStringLiteral("DELETE FROM receipts")) && exec(QStringLiteral("DELETE FROM transactions")) &&
           exec(QStringLiteral("DELETE FROM categories")) && exec(QStringLiteral("DELETE FROM recurring")) &&
           exec(QStringLiteral("DELETE FROM accounts")) && exec(QStringLiteral("DELETE FROM import_rules")) &&
           exec(QStringLiteral("DELETE FROM import_seen"));
}

bool Store::replaceAll(const BackupData &data, QString *error)
{
    m_db.transaction();
    bool ok = clearTables();
    for (const auto &c : data.categories) ok = ok && writeCategory(c);
    for (const auto &t : data.transactions) ok = ok && writeTransaction(t);
    for (const auto &r : data.receipts) ok = ok && writeReceipt(r);
    for (const auto &r : data.recurring) ok = ok && writeRecurring(r);
    for (const auto &a : data.accounts) ok = ok && writeAccount(a);
    for (const auto &r : data.importRules) ok = ok && writeImportRule(r);
    if (!ok || !m_db.commit()) {
        if (error) *error = m_db.lastError().text();
        m_db.rollback();
        return false;
    }
    m_autoPosted.clear();
    Settings s = data.settings;
    s.lastBackupAt = util::nowIso();
    // keep this computer's auto-backup configuration
    s.autoBackupDir = m_settings.autoBackupDir;
    s.autoBackupKeep = m_settings.autoBackupKeep;
    saveSettings(s);
    loadCache();
    processRecurring();
    markDirty();
    emit dataChanged();
    return true;
}

bool Store::merge(const BackupData &data, MergeStats *stats, QString *error)
{
    QHash<QString, QString> byKey; // type|name -> id
    QSet<QString> catIds;
    for (const auto &c : m_categories) {
        byKey.insert(typeToString(c.type) + QLatin1Char('|') + c.name.toLower(), c.id);
        catIds.insert(c.id);
    }
    QHash<QString, QString> remap;
    QVector<Category> newCats;
    for (const auto &c : data.categories) {
        const QString key = typeToString(c.type) + QLatin1Char('|') + c.name.toLower();
        if (catIds.contains(c.id)) continue;
        if (byKey.contains(key)) {
            remap.insert(c.id, byKey.value(key));
            continue;
        }
        newCats.push_back(c);
        catIds.insert(c.id);
        byKey.insert(key, c.id);
    }
    QSet<QString> txIds;
    for (const auto &t : m_transactions) txIds.insert(t.id);
    QVector<Transaction> newTx;
    QSet<QString> newTxIds;
    for (Transaction t : data.transactions) {
        if (txIds.contains(t.id)) continue;
        t.categoryId = remap.value(t.categoryId, t.categoryId);
        newTx.push_back(t);
        newTxIds.insert(t.id);
    }
    QVector<Receipt> newReceipts;
    for (const auto &r : data.receipts)
        if (newTxIds.contains(r.transactionId)) newReceipts.push_back(r);
    QSet<QString> ruleIds;
    for (const auto &r : m_recurring) ruleIds.insert(r.id);
    QVector<Recurring> newRules;
    for (Recurring r : data.recurring) {
        if (ruleIds.contains(r.id)) continue;
        r.categoryId = remap.value(r.categoryId, r.categoryId);
        newRules.push_back(r);
    }
    QVector<Account> newAccounts;
    for (const auto &a : data.accounts)
        if (!account(a.id)) newAccounts.push_back(a);
    QSet<QString> irIds, irPatterns;
    for (const auto &r : m_importRules) {
        irIds.insert(r.id);
        irPatterns.insert(r.pattern.toLower());
    }
    QVector<ImportRule> newImportRules;
    for (ImportRule r : data.importRules) {
        if (irIds.contains(r.id) || irPatterns.contains(r.pattern.toLower())) continue;
        r.categoryId = remap.value(r.categoryId, r.categoryId);
        newImportRules.push_back(r);
    }

    m_db.transaction();
    bool ok = true;
    for (const auto &c : newCats) ok = ok && writeCategory(c);
    for (const auto &t : newTx) ok = ok && writeTransaction(t);
    for (const auto &r : newReceipts) ok = ok && writeReceipt(r);
    for (const auto &r : newRules) ok = ok && writeRecurring(r);
    for (const auto &a : newAccounts) ok = ok && writeAccount(a);
    for (const auto &r : newImportRules) ok = ok && writeImportRule(r);
    if (!ok || !m_db.commit()) {
        if (error) *error = m_db.lastError().text();
        m_db.rollback();
        return false;
    }
    loadCache();
    processRecurring();
    if (stats) *stats = {static_cast<int>(newCats.size()), static_cast<int>(newTx.size()), static_cast<int>(newReceipts.size()), static_cast<int>(newRules.size())};
    loadCache();
    markDirty();
    emit dataChanged();
    return true;
}

bool Store::eraseAll(QString *error)
{
    m_db.transaction();
    if (!clearTables() || !m_db.commit()) {
        if (error) *error = m_db.lastError().text();
        m_db.rollback();
        return false;
    }
    m_autoPosted.clear();
    Settings fresh;
    fresh.autoBackupDir = m_settings.autoBackupDir;
    fresh.autoBackupKeep = m_settings.autoBackupKeep;
    loadCache();
    seedDefaultCategories(true);
    saveSettings(fresh);
    markDirty();
    emit dataChanged();
    return true;
}

int Store::loadSampleData()
{
    if (!m_transactions.isEmpty()) return 0;
    seedDefaultCategories(false);
    auto byName = [this](const QString &n) -> const Category * {
        for (const auto &c : m_categories)
            if (c.name == n) return &c;
        return nullptr;
    };
    std::mt19937 rng(42);
    std::uniform_real_distribution<double> uni(0.0, 1.0);
    auto rnd = [&]() { return uni(rng); };
    auto between = [&](double a, double b) { return static_cast<qint64>(std::llround((a + rnd() * (b - a)) * 100.0)); };
    auto pick = [&](const QStringList &l) { return l.at(static_cast<int>(rnd() * l.size()) % l.size()); };

    QVector<Transaction> out;
    const QString now = util::nowIso();
    auto add = [&](const QDate &d, const QString &cat, qint64 amount, const QString &desc, const QString &vendor, const QString &pm) {
        const Category *c = byName(cat);
        if (!c) return;
        Transaction t;
        t.id = util::newId();
        t.type = c->type;
        t.categoryId = c->id;
        t.amount = amount;
        t.date = d;
        t.description = desc;
        t.vendor = vendor;
        t.paymentMethod = pm;
        t.createdAt = now;
        t.updatedAt = now;
        out.push_back(t);
    };

    struct Fixed { int d; const char *cat; qint64 amt; const char *desc; const char *vendor; const char *pm; };
    const QDate today = QDate::currentDate();
    for (int k = 5; k >= 0; --k) {
        const QDate first = QDate(today.year(), today.month(), 1).addMonths(-k);
        const int dim = first.daysInMonth();
        auto day = [&](int d) { return QDate(first.year(), first.month(), std::min(d, dim)); };
        // Fixed monthly costs (rent, internet, payroll...) come from recurring rules below.
        const Fixed fixed[] = {
            {12, "Utilities", between(140, 260), "Electricity bill", "City Power", "Bank transfer"},
            {20, "Advertising & Marketing", between(150, 450), "Social media ads", "AdReach", "Credit card"},
        };
        for (const auto &f : fixed)
            if (day(f.d) <= today)
                add(day(f.d), QString::fromUtf8(f.cat), f.amt, QString::fromUtf8(f.desc), QString::fromUtf8(f.vendor), QString::fromUtf8(f.pm));
        for (int d = 1; d <= dim; ++d) {
            const QDate date = day(d);
            if (date > today) break;
            if (date.dayOfWeek() != 7) {
                const int n = 1 + static_cast<int>(rnd() * 3);
                for (int i = 0; i < n; ++i)
                    add(date, QStringLiteral("Sales Revenue"), between(120, 900),
                        pick({QStringLiteral("Retail sales"), QStringLiteral("Online order"), QStringLiteral("Counter sales"), QStringLiteral("Wholesale order")}),
                        pick({QStringLiteral("Walk-in"), QStringLiteral("Web store"), QStringLiteral("Market stall"), QString()}),
                        pick({QStringLiteral("Cash"), QStringLiteral("Debit card"), QStringLiteral("Credit card"), QStringLiteral("Mobile payment")}));
            }
            if (rnd() < 0.18) add(date, QStringLiteral("Service Income"), between(250, 1500), pick({QStringLiteral("Installation job"), QStringLiteral("Repair service"), QStringLiteral("Maintenance contract")}), pick({QStringLiteral("Greenfield Ltd"), QStringLiteral("J. Patel"), QStringLiteral("Northside Cafe"), QStringLiteral("Harbor Dental")}), QStringLiteral("Bank transfer"));
            if (rnd() < 0.30) add(date, QStringLiteral("Office Supplies"), between(8, 85), pick({QStringLiteral("Printer paper"), QStringLiteral("Ink cartridges"), QStringLiteral("Stationery"), QStringLiteral("Cleaning supplies")}), pick({QStringLiteral("OfficeMart"), QStringLiteral("Staples Depot"), QStringLiteral("Corner Store")}), pick({QStringLiteral("Cash"), QStringLiteral("Debit card")}));
            if (rnd() < 0.22) add(date, QStringLiteral("Vehicle & Fuel"), between(30, 95), QStringLiteral("Fuel"), pick({QStringLiteral("Shell"), QStringLiteral("Petro Plus"), QStringLiteral("GasGo")}), QStringLiteral("Debit card"));
            if (rnd() < 0.15) add(date, QStringLiteral("Meals & Entertainment"), between(15, 120), pick({QStringLiteral("Client lunch"), QStringLiteral("Team coffee"), QStringLiteral("Supplier meeting")}), pick({QStringLiteral("Bistro 21"), QStringLiteral("Bean There"), QStringLiteral("Golden Wok")}), QStringLiteral("Credit card"));
            if (rnd() < 0.12) add(date, QStringLiteral("Cost of Goods / Inventory"), between(300, 2200), QStringLiteral("Stock purchase"), pick({QStringLiteral("Acme Wholesale"), QStringLiteral("Prime Distributors")}), QStringLiteral("Bank transfer"));
            if (rnd() < 0.08) add(date, QStringLiteral("Shipping & Postage"), between(12, 70), QStringLiteral("Courier"), pick({QStringLiteral("FastShip"), QStringLiteral("Post Office")}), QStringLiteral("Debit card"));
            if (rnd() < 0.05) add(date, QStringLiteral("Bank & Merchant Fees"), between(5, 40), QStringLiteral("Card processing fees"), QStringLiteral("Bank"), QStringLiteral("Bank transfer"));
            if (rnd() < 0.03) add(date, QStringLiteral("Repairs & Maintenance"), between(80, 400), QStringLiteral("Equipment repair"), QStringLiteral("FixIt Services"), QStringLiteral("Cash"));
        }
    }

    m_db.transaction();
    for (const auto &t : out) writeTransaction(t);
    const std::pair<const char *, qint64> budgets[] = {{"Office Supplies", 40000}, {"Vehicle & Fuel", 50000}, {"Meals & Entertainment", 25000}, {"Advertising & Marketing", 40000}};
    for (const auto &b : budgets) {
        if (const Category *c = byName(QString::fromUtf8(b.first))) {
            Category cc = *c;
            cc.budget = b.second;
            writeCategory(cc);
        }
    }
    // Recurring rules starting six months ago; processing them records every past payment.
    const QDate start = QDate(today.year(), today.month(), 1).addMonths(-5);
    const Fixed rules[] = {
        {1, "Rent & Lease", 180000, "Monthly shop rent", "Oakview Properties", "Bank transfer"},
        {3, "Software & Subscriptions", 4900, "Accounting software", "CloudBooks", "Credit card"},
        {5, "Telephone & Internet", 8900, "Business internet", "NetLink", "Bank transfer"},
        {8, "Insurance", 21500, "Liability insurance", "SafeGuard Insurance", "Bank transfer"},
        {15, "Payroll & Wages", 320000, "Payroll - 1st half", "Staff", "Bank transfer"},
        {28, "Payroll & Wages", 320000, "Payroll - 2nd half", "Staff", "Bank transfer"},
    };
    for (const auto &f : rules) {
        const Category *c = byName(QString::fromUtf8(f.cat));
        if (!c) continue;
        Recurring r;
        r.id = util::newId();
        r.type = c->type;
        r.categoryId = c->id;
        r.amount = f.amt;
        r.description = QString::fromUtf8(f.desc);
        r.vendor = QString::fromUtf8(f.vendor);
        r.paymentMethod = QString::fromUtf8(f.pm);
        r.frequency = Frequency::Monthly;
        r.day = f.d;
        r.startDate = QDate(start.year(), start.month(), f.d);
        r.nextDate = r.startDate;
        r.createdAt = now;
        r.updatedAt = now;
        writeRecurring(r);
    }
    m_db.commit();
    loadCache();
    const int posted = static_cast<int>(processRecurring(today, false).size());
    markDirty();
    emit dataChanged();
    return static_cast<int>(out.size()) + posted;
}

/* ---------------- Recurring ---------------- */

void Store::sortRecurring()
{
    std::stable_sort(m_recurring.begin(), m_recurring.end(), [](const Recurring &a, const Recurring &b) {
        if (a.active != b.active) return a.active;
        const QDate an = a.finished() ? QDate(9999, 1, 1) : a.nextDate;
        const QDate bn = b.finished() ? QDate(9999, 1, 1) : b.nextDate;
        if (an != bn) return an < bn;
        return QString::localeAwareCompare(a.title(), b.title()) < 0;
    });
}

const Recurring *Store::recurringRule(const QString &id) const
{
    for (const auto &r : m_recurring)
        if (r.id == id) return &r;
    return nullptr;
}

int Store::recurringPostedCount(const QString &id) const
{
    int n = 0;
    for (const auto &t : m_transactions)
        if (t.recurringId == id) ++n;
    return n;
}

bool Store::saveRecurring(const Recurring &input, QString *error, const QDate &today)
{
    Recurring r = input;
    if (r.amount <= 0) {
        if (error) *error = QStringLiteral("Enter an amount greater than zero.");
        return false;
    }
    const Category *c = category(r.categoryId);
    if (!c) {
        if (error) *error = QStringLiteral("Choose a category.");
        return false;
    }
    if (!r.startDate.isValid()) {
        if (error) *error = QStringLiteral("Choose the date of the first payment.");
        return false;
    }
    if (r.endDate.isValid() && r.endDate < r.startDate) {
        if (error) *error = QStringLiteral("The end date is before the first payment.");
        return false;
    }
    r.type = c->type;
    if (r.frequency != Frequency::Weekly) r.day = r.startDate.day();
    const QString now = util::nowIso();
    if (r.id.isEmpty()) r.id = util::newId();
    if (r.createdAt.isEmpty()) r.createdAt = now;
    r.updatedAt = now;

    const Recurring *old = recurringRule(r.id);
    if (!old) {
        if (!r.nextDate.isValid()) r.nextDate = r.startDate; // first payment not recorded yet
    } else {
        const bool scheduleChanged = old->frequency != r.frequency || old->startDate != r.startDate;
        if (!scheduleChanged) {
            r.nextDate = old->nextDate;
        } else if (recurringPostedCount(r.id) > 0) {
            // Past payments are already recorded; continue the new schedule from today.
            r.nextDate = recurrence::firstOnOrAfter(r, std::max(today, r.startDate));
        } else {
            r.nextDate = r.startDate;
        }
    }

    if (!writeRecurring(r)) {
        if (error) *error = m_db.lastError().text();
        return false;
    }
    bool found = false;
    for (auto &x : m_recurring)
        if (x.id == r.id) {
            x = r;
            found = true;
        }
    if (!found) m_recurring.push_back(r);
    sortRecurring();
    markDirty();
    if (processRecurring(today, false).isEmpty()) emit dataChanged();
    return true;
}

bool Store::makeRecurring(const QString &txId, Frequency frequency, bool recordMissed, QString *error, const QDate &today, QString *ruleId)
{
    const Transaction *tp = transaction(txId);
    if (!tp) {
        if (error) *error = QStringLiteral("Transaction not found.");
        return false;
    }
    if (!tp->recurringId.isEmpty() && recurringRule(tp->recurringId)) {
        if (error) *error = QStringLiteral("This transaction is already part of a recurring series.");
        return false;
    }
    Transaction t = *tp;
    Recurring r;
    r.id = util::newId();
    r.type = t.type;
    r.categoryId = t.categoryId;
    r.amount = t.amount;
    r.description = t.description;
    if (r.description.isEmpty() && t.vendor.isEmpty()) r.description = categoryOrUnknown(t.categoryId).name;
    r.vendor = t.vendor;
    r.paymentMethod = t.paymentMethod;
    r.notes = t.notes;
    r.frequency = frequency;
    r.startDate = t.date;
    r.day = t.date.day();
    r.nextDate = recurrence::advance(t.date, frequency, r.day);
    if (!recordMissed) {
        int guard = 0;
        while (r.nextDate <= today && guard++ < 5000) r.nextDate = recurrence::advance(r.nextDate, frequency, r.day);
    }

    // Link the transaction as the series' first payment
    t.recurringId = r.id;
    t.updatedAt = util::nowIso();
    if (!writeTransaction(t)) {
        if (error) *error = m_db.lastError().text();
        return false;
    }
    for (auto &x : m_transactions)
        if (x.id == t.id) x = t;
    if (!saveRecurring(r, error, today)) {
        // roll back the link
        t.recurringId.clear();
        writeTransaction(t);
        for (auto &x : m_transactions)
            if (x.id == t.id) x = t;
        return false;
    }
    if (ruleId) *ruleId = r.id;
    return true;
}

bool Store::setRecurringActive(const QString &id, bool active, const QDate &today)
{
    const Recurring *old = recurringRule(id);
    if (!old || old->active == active) return old != nullptr;
    Recurring r = *old;
    r.active = active;
    r.updatedAt = util::nowIso();
    // Resuming skips the payments that fell due while paused.
    if (active && r.nextDate.isValid() && r.nextDate < today) r.nextDate = recurrence::firstOnOrAfter(r, today);
    if (!writeRecurring(r)) return false;
    for (auto &x : m_recurring)
        if (x.id == id) x = r;
    sortRecurring();
    markDirty();
    emit dataChanged();
    return true;
}

bool Store::deleteRecurring(const QString &id, QString *error)
{
    m_db.transaction();
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("DELETE FROM recurring WHERE id = ?"));
    q.addBindValue(id);
    bool ok = q.exec();
    if (ok) {
        // Keep the payments already recorded, just unlink them.
        q.prepare(QStringLiteral("UPDATE transactions SET recurring_id = NULL WHERE recurring_id = ?"));
        q.addBindValue(id);
        ok = q.exec();
    }
    if (ok) {
        trackChange(QStringLiteral("recurring"), id);
        for (const auto &t : m_transactions)
            if (t.recurringId == id) trackChange(QStringLiteral("transaction"), t.id);
    }
    if (!ok || !m_db.commit()) {
        if (error) *error = q.lastError().text();
        m_db.rollback();
        return false;
    }
    m_recurring.erase(std::remove_if(m_recurring.begin(), m_recurring.end(), [&](const Recurring &r) { return r.id == id; }), m_recurring.end());
    for (auto &t : m_transactions)
        if (t.recurringId == id) t.recurringId.clear();
    markDirty();
    emit dataChanged();
    return true;
}

QVector<Transaction> Store::processRecurring(const QDate &today, bool notify)
{
    QVector<Transaction> posted;
    if (!m_db.isOpen()) return posted;
    const QString now = util::nowIso();
    m_db.transaction();
    for (auto &r : m_recurring) {
        if (!r.active || !category(r.categoryId)) continue;
        bool changed = false;
        int guard = 0;
        while (r.nextDate.isValid() && r.nextDate <= today && !r.finished() && guard++ < 1000) {
            Transaction t;
            // Same id on every computer, so two computers sharing a folder never record a payment twice.
            t.id = util::stableId(QStringLiteral("recurring|%1|%2").arg(r.id, r.nextDate.toString(Qt::ISODate)));
            t.type = category(r.categoryId)->type;
            t.categoryId = r.categoryId;
            t.amount = r.amount;
            t.date = r.nextDate;
            t.description = r.description;
            t.vendor = r.vendor;
            t.paymentMethod = r.paymentMethod;
            t.notes = r.notes;
            t.createdAt = now;
            t.updatedAt = now;
            t.recurringId = r.id;
            // Auto-recorded payments carry the due date as their change time, so a real edit
            // made on any computer always counts as newer.
            m_tsOverride = QDateTime(r.nextDate, QTime(0, 0), QTimeZone::utc()).toMSecsSinceEpoch();
            const bool written = writeTransaction(t);
            m_tsOverride = 0;
            if (!written) break;
            posted.push_back(t);
            r.nextDate = recurrence::advance(r.nextDate, r.frequency, r.day);
            changed = true;
        }
        if (changed) {
            r.updatedAt = now;
            writeRecurring(r);
        }
    }
    if (!m_db.commit()) {
        m_db.rollback();
        loadCache();
        return {};
    }
    if (posted.isEmpty()) return posted;
    m_transactions += posted;
    sortTransactions();
    sortRecurring();
    if (notify) m_autoPosted += posted;
    markDirty();
    emit dataChanged();
    return posted;
}

QVector<std::pair<QDate, Recurring>> Store::upcoming(const QDate &from, const QDate &to) const
{
    QVector<std::pair<QDate, Recurring>> out;
    for (const auto &r : m_recurring) {
        if (!r.active) continue;
        for (const QDate &d : recurrence::occurrences(r, from, to, 60)) out.push_back({d, r});
    }
    std::stable_sort(out.begin(), out.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
    return out;
}

/* ---------------- Aggregations ---------------- */

QVector<Transaction> Store::filtered(const std::function<bool(const Transaction &)> &pred) const
{
    QVector<Transaction> out;
    for (const auto &t : m_transactions)
        if (pred(t)) out.push_back(t);
    return out;
}

QVector<Transaction> Store::inMonth(int year, int month) const
{
    return filtered([=](const Transaction &t) { return t.date.year() == year && t.date.month() == month; });
}

QVector<Transaction> Store::inYear(int year) const
{
    return filtered([=](const Transaction &t) { return t.date.year() == year; });
}

QVector<Transaction> Store::onDay(const QDate &d) const
{
    return filtered([=](const Transaction &t) { return t.date == d; });
}

Totals Store::totals(const QVector<Transaction> &txs)
{
    Totals r;
    for (const auto &t : txs) {
        if (t.type == TxType::Income) r.income += t.amount;
        else r.expense += t.amount;
        ++r.count;
    }
    return r;
}

QHash<QString, CategoryTotal> Store::byCategory(const QVector<Transaction> &txs)
{
    QHash<QString, CategoryTotal> m;
    for (const auto &t : txs) {
        auto &e = m[t.categoryId];
        e.total += t.amount;
        ++e.count;
    }
    return m;
}

std::array<Totals, 12> Store::monthly(int year) const
{
    std::array<Totals, 12> out{};
    for (const auto &t : m_transactions) {
        if (t.date.year() != year) continue;
        auto &m = out[static_cast<size_t>(t.date.month() - 1)];
        if (t.type == TxType::Income) m.income += t.amount;
        else m.expense += t.amount;
        ++m.count;
    }
    return out;
}

QList<int> Store::yearsWithData() const
{
    QSet<int> ys;
    for (const auto &t : m_transactions) ys.insert(t.date.year());
    ys.insert(QDate::currentDate().year());
    QList<int> out(ys.begin(), ys.end());
    std::sort(out.begin(), out.end());
    return out;
}

QStringList Store::knownVendors() const
{
    QSet<QString> seen;
    QStringList out;
    for (const auto &t : m_transactions)
        if (!t.vendor.isEmpty() && !seen.contains(t.vendor)) {
            seen.insert(t.vendor);
            out << t.vendor;
        }
    return out;
}

bool Store::backupIsStale() const
{
    const int days = m_settings.backupReminderDays;
    if (days <= 0 || m_transactions.isEmpty()) return false;
    if (m_settings.lastBackupAt.isEmpty()) return true;
    const QDateTime last = QDateTime::fromString(m_settings.lastBackupAt, Qt::ISODateWithMs);
    if (!last.isValid()) return true;
    return last.secsTo(QDateTime::currentDateTimeUtc()) >= static_cast<qint64>(days) * 86400;
}

/* ---------------- Bank accounts & import rules ---------------- */

const Account *Store::account(const QString &id) const
{
    for (const auto &a : m_accounts)
        if (a.id == id) return &a;
    return nullptr;
}

bool Store::writeAccount(const Account &a)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("INSERT OR REPLACE INTO accounts(id, name, kind, flip_signs, csv_mapping, created_at, updated_at) VALUES(?,?,?,?,?,?,?)"));
    q.addBindValue(a.id);
    q.addBindValue(a.name);
    q.addBindValue(a.kind);
    q.addBindValue(a.flipSigns ? 1 : 0);
    q.addBindValue(a.csvMapping);
    q.addBindValue(a.createdAt);
    q.addBindValue(a.updatedAt);
    const bool ok = q.exec();
    if (ok) trackChange(QStringLiteral("account"), a.id);
    return ok;
}

bool Store::saveAccount(const Account &input, QString *error, QString *savedId)
{
    Account a = input;
    a.name = a.name.simplified();
    if (a.name.isEmpty()) {
        if (error) *error = QStringLiteral("Enter a name for the account, e.g. \"Business Visa\".");
        return false;
    }
    for (const auto &x : m_accounts)
        if (x.id != a.id && x.name.compare(a.name, Qt::CaseInsensitive) == 0) {
            if (error) *error = QStringLiteral("An account named \"%1\" already exists.").arg(a.name);
            return false;
        }
    const QString now = util::nowIso();
    if (a.id.isEmpty()) a.id = util::newId();
    if (a.createdAt.isEmpty()) a.createdAt = now;
    a.updatedAt = now;
    if (!writeAccount(a)) {
        if (error) *error = m_db.lastError().text();
        return false;
    }
    bool found = false;
    for (auto &x : m_accounts)
        if (x.id == a.id) {
            x = a;
            found = true;
        }
    if (!found) m_accounts.push_back(a);
    std::sort(m_accounts.begin(), m_accounts.end(), [](const Account &x, const Account &y) { return QString::localeAwareCompare(x.name, y.name) < 0; });
    if (savedId) *savedId = a.id;
    markDirty();
    emit dataChanged();
    return true;
}

bool Store::deleteAccount(const QString &id, QString *error)
{
    m_db.transaction();
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("DELETE FROM accounts WHERE id = ?"));
    q.addBindValue(id);
    bool ok = q.exec();
    if (ok) {
        q.prepare(QStringLiteral("DELETE FROM import_seen WHERE account_id = ?"));
        q.addBindValue(id);
        ok = q.exec();
    }
    if (ok) {
        trackChange(QStringLiteral("account"), id);
        // Imported transactions stay, they just no longer point at the account.
        for (Transaction t : m_transactions) {
            if (t.accountId != id) continue;
            t.accountId.clear();
            ok = ok && writeTransaction(t);
        }
    }
    if (!ok || !m_db.commit()) {
        if (error) *error = m_db.lastError().text();
        m_db.rollback();
        return false;
    }
    loadCache();
    markDirty();
    emit dataChanged();
    return true;
}

int Store::accountTransactionCount(const QString &accountId) const
{
    int n = 0;
    for (const auto &t : m_transactions)
        if (t.accountId == accountId) ++n;
    return n;
}

bool Store::writeImportRule(const ImportRule &r)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("INSERT OR REPLACE INTO import_rules(id, pattern, category_id, ignore, created_at, updated_at) VALUES(?,?,?,?,?,?)"));
    q.addBindValue(r.id);
    q.addBindValue(r.pattern);
    q.addBindValue(r.categoryId);
    q.addBindValue(r.ignore ? 1 : 0);
    q.addBindValue(r.createdAt);
    q.addBindValue(r.updatedAt);
    const bool ok = q.exec();
    if (ok) trackChange(QStringLiteral("rule"), r.id);
    return ok;
}

bool Store::saveImportRule(const ImportRule &input, QString *error)
{
    ImportRule r = input;
    r.pattern = r.pattern.simplified();
    if (r.pattern.size() < 2) {
        if (error) *error = QStringLiteral("Enter at least 2 characters to look for in the bank description.");
        return false;
    }
    if (!r.ignore && !category(r.categoryId)) {
        if (error) *error = QStringLiteral("Choose a category, or choose to ignore matching lines.");
        return false;
    }
    for (const auto &x : m_importRules)
        if (x.id != r.id && x.pattern.compare(r.pattern, Qt::CaseInsensitive) == 0) {
            if (error) *error = QStringLiteral("There is already a rule for \"%1\".").arg(r.pattern);
            return false;
        }
    const QString now = util::nowIso();
    if (r.id.isEmpty()) r.id = util::newId();
    if (r.createdAt.isEmpty()) r.createdAt = now;
    r.updatedAt = now;
    if (r.ignore) r.categoryId.clear();
    if (!writeImportRule(r)) {
        if (error) *error = m_db.lastError().text();
        return false;
    }
    bool found = false;
    for (auto &x : m_importRules)
        if (x.id == r.id) {
            x = r;
            found = true;
        }
    if (!found) m_importRules.push_back(r);
    std::sort(m_importRules.begin(), m_importRules.end(), [](const ImportRule &a, const ImportRule &b) { return a.pattern.compare(b.pattern, Qt::CaseInsensitive) < 0; });
    markDirty();
    emit dataChanged();
    return true;
}

bool Store::deleteImportRule(const QString &id)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("DELETE FROM import_rules WHERE id = ?"));
    q.addBindValue(id);
    if (!q.exec()) return false;
    trackChange(QStringLiteral("rule"), id);
    m_importRules.erase(std::remove_if(m_importRules.begin(), m_importRules.end(), [&](const ImportRule &r) { return r.id == id; }), m_importRules.end());
    markDirty();
    emit dataChanged();
    return true;
}

bool Store::alreadyImported(const QString &accountId, const QString &externalId) const
{
    return !externalId.isEmpty() && m_importedKeys.contains(accountId + QLatin1Char('|') + externalId);
}

int Store::importTransactions(const QVector<Transaction> &txs, QString *error)
{
    if (txs.isEmpty()) return 0;
    const QString now = util::nowIso();
    QVector<Transaction> done;
    m_db.transaction();
    bool ok = true;
    for (Transaction t : txs) {
        if (!category(t.categoryId) || t.amount <= 0 || !t.date.isValid()) continue;
        if (alreadyImported(t.accountId, t.externalId)) continue;
        t.type = category(t.categoryId)->type;
        if (t.id.isEmpty()) t.id = util::newId();
        if (t.createdAt.isEmpty()) t.createdAt = now;
        t.updatedAt = now;
        ok = writeTransaction(t);
        if (!ok) break;
        if (!t.externalId.isEmpty()) {
            QSqlQuery q(m_db);
            q.prepare(QStringLiteral("INSERT OR IGNORE INTO import_seen(account_id, external_id) VALUES(?,?)"));
            q.addBindValue(t.accountId);
            q.addBindValue(t.externalId);
            q.exec();
        }
        done.push_back(t);
    }
    if (!ok || !m_db.commit()) {
        if (error) *error = m_db.lastError().text();
        m_db.rollback();
        loadCache();
        return -1;
    }
    m_transactions += done;
    sortTransactions();
    if (!done.isEmpty()) {
        markDirty();
        emit dataChanged();
    }
    return static_cast<int>(done.size());
}

/* ---------------- Local key/value ---------------- */

QString Store::localValue(const QString &key) const
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT value FROM local WHERE key = ?"));
    q.addBindValue(key);
    if (q.exec() && q.next()) return q.value(0).toString();
    return {};
}

void Store::setLocalValue(const QString &key, const QString &value)
{
    QSqlQuery q(m_db);
    if (value.isEmpty()) {
        q.prepare(QStringLiteral("DELETE FROM local WHERE key = ?"));
        q.addBindValue(key);
    } else {
        q.prepare(QStringLiteral("INSERT OR REPLACE INTO local(key, value) VALUES(?, ?)"));
        q.addBindValue(key);
        q.addBindValue(value);
    }
    q.exec();
}

/* ---------------- Sync support ---------------- */

bool Store::addColumnIfMissing(const QString &table, const QString &column, const QString &type, QString *error)
{
    QSqlQuery info(m_db);
    if (info.exec(QStringLiteral("PRAGMA table_info(%1)").arg(table)))
        while (info.next())
            if (info.value(1).toString() == column) return true;
    return exec(QStringLiteral("ALTER TABLE %1 ADD COLUMN %2 %3").arg(table, column, type), error);
}

QStringList Store::receiptIdsForTransactions(const QStringList &txIds) const
{
    QStringList out;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT id FROM receipts WHERE transaction_id = ?"));
    for (const QString &id : txIds) {
        q.bindValue(0, id);
        if (q.exec())
            while (q.next()) out << q.value(0).toString();
    }
    return out;
}

qint64 Store::nextTs()
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    m_lastTs = std::max(now, m_lastTs + 1);
    return m_lastTs;
}

void Store::trackChange(const QString &entity, const QString &id)
{
    if (!m_tracking || m_applyingRemote || id.isEmpty()) return;
    const qint64 ts = m_tsOverride ? m_tsOverride : nextTs();
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("INSERT OR REPLACE INTO sync_outbox(entity, id, ts) VALUES(?,?,?)"));
    q.addBindValue(entity);
    q.addBindValue(id);
    q.addBindValue(ts);
    q.exec();
    q.prepare(QStringLiteral("INSERT OR REPLACE INTO sync_versions(entity, id, ts, device) VALUES(?,?,?,?)"));
    q.addBindValue(entity);
    q.addBindValue(id);
    q.addBindValue(ts);
    q.addBindValue(m_deviceId);
    q.exec();
}

void Store::trackAllExisting()
{
    if (!m_tracking || m_applyingRemote) return;
    const std::pair<const char *, const char *> tables[] = {
        {"category", "categories"}, {"transaction", "transactions"}, {"receipt", "receipts"},
        {"recurring", "recurring"}, {"account", "accounts"},         {"rule", "import_rules"},
    };
    for (const auto &t : tables) {
        QSqlQuery q(m_db);
        QStringList ids;
        if (q.exec(QStringLiteral("SELECT id FROM %1").arg(QString::fromLatin1(t.second))))
            while (q.next()) ids << q.value(0).toString();
        for (const QString &id : ids) trackChange(QString::fromLatin1(t.first), id);
    }
}

void Store::trackEverything()
{
    m_db.transaction();
    trackAllExisting();
    trackChange(QStringLiteral("settings"), QStringLiteral("business"));
    m_db.commit();
}

QVector<Store::Change> Store::pendingChanges(int limit) const
{
    QVector<Change> out;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT entity, id, ts FROM sync_outbox ORDER BY ts LIMIT ?"));
    q.addBindValue(limit);
    if (q.exec())
        while (q.next()) out.push_back({q.value(0).toString(), q.value(1).toString(), q.value(2).toLongLong()});
    return out;
}

int Store::pendingChangeCount() const
{
    QSqlQuery q(m_db);
    if (q.exec(QStringLiteral("SELECT COUNT(*) FROM sync_outbox")) && q.next()) return q.value(0).toInt();
    return 0;
}

void Store::clearPendingChanges(const QVector<Change> &done)
{
    m_db.transaction();
    QSqlQuery q(m_db);
    // Only remove entries that were not changed again in the meantime.
    q.prepare(QStringLiteral("DELETE FROM sync_outbox WHERE entity = ? AND id = ? AND ts = ?"));
    for (const auto &c : done) {
        q.bindValue(0, c.entity);
        q.bindValue(1, c.id);
        q.bindValue(2, c.ts);
        q.exec();
    }
    m_db.commit();
}

QJsonObject Store::recordJson(const QString &entity, const QString &id, bool *exists) const
{
    *exists = true;
    if (entity == QLatin1String("category")) {
        if (const Category *c = category(id)) return json::from(*c);
    } else if (entity == QLatin1String("transaction")) {
        if (const Transaction *t = transaction(id)) return json::from(*t);
    } else if (entity == QLatin1String("recurring")) {
        if (const Recurring *r = recurringRule(id)) return json::from(*r);
    } else if (entity == QLatin1String("account")) {
        if (const Account *a = account(id)) return json::from(*a);
    } else if (entity == QLatin1String("rule")) {
        for (const auto &r : m_importRules)
            if (r.id == id) return json::from(r);
    } else if (entity == QLatin1String("receipt")) {
        QSqlQuery q(m_db);
        q.prepare(QStringLiteral("SELECT id, transaction_id, name, mime, size, created_at FROM receipts WHERE id = ?"));
        q.addBindValue(id);
        if (q.exec() && q.next()) {
            Receipt r;
            r.id = q.value(0).toString();
            r.transactionId = q.value(1).toString();
            r.name = q.value(2).toString();
            r.mime = q.value(3).toString();
            r.size = q.value(4).toLongLong();
            r.createdAt = q.value(5).toString();
            return json::receiptMeta(r);
        }
    } else if (entity == QLatin1String("settings")) {
        QJsonObject o;
        o[QStringLiteral("businessName")] = m_settings.businessName;
        o[QStringLiteral("currency")] = m_settings.currency;
        return o;
    }
    *exists = false;
    return {};
}

QByteArray Store::receiptData(const QString &id) const
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT data FROM receipts WHERE id = ?"));
    q.addBindValue(id);
    if (q.exec() && q.next()) return q.value(0).toByteArray();
    return {};
}

void Store::beginRemoteBatch()
{
    m_applyingRemote = true;
    m_db.transaction();
}

void Store::endRemoteBatch(bool changed)
{
    m_db.commit();
    m_applyingRemote = false;
    if (changed) {
        loadCache();
        markDirty();
        emit dataChanged();
        emit settingsChanged();
    }
}

Store::ApplyResult Store::applyRemote(const QString &entity, const QString &id, qint64 ts, const QString &device, bool deleted,
                                      const QJsonObject &data, const std::function<bool(QByteArray *)> &loadReceipt)
{
    // Last writer wins: skip if our copy is as new or newer.
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT ts, device FROM sync_versions WHERE entity = ? AND id = ?"));
    q.addBindValue(entity);
    q.addBindValue(id);
    if (q.exec() && q.next()) {
        const qint64 ours = q.value(0).toLongLong();
        const QString ourDevice = q.value(1).toString();
        if (ts < ours || (ts == ours && device <= ourDevice)) return ApplyResult::Skipped;
    }

    auto del = [&](const QString &sql) {
        QSqlQuery d(m_db);
        d.prepare(sql);
        d.addBindValue(id);
        return d.exec();
    };
    bool ok = true;
    if (entity == QLatin1String("settings")) {
        if (!deleted) {
            const QString name = data.value(QStringLiteral("businessName")).toString().left(80);
            if (!name.isEmpty()) m_settings.businessName = name;
            const QString cur = data.value(QStringLiteral("currency")).toString();
            for (const auto &c : defaults::currencies())
                if (c.code == cur) m_settings.currency = cur;
            util::setCurrency(m_settings.currency);
            QSqlQuery w(m_db);
            w.prepare(QStringLiteral("INSERT OR REPLACE INTO meta(key, value) VALUES('settings', ?)"));
            w.addBindValue(QString::fromUtf8(QJsonDocument(settingsToJson(m_settings)).toJson(QJsonDocument::Compact)));
            ok = w.exec();
        }
    } else if (entity == QLatin1String("category")) {
        Category c;
        ok = deleted ? del(QStringLiteral("DELETE FROM categories WHERE id = ?")) : (json::to(data, &c) && writeCategory(c));
    } else if (entity == QLatin1String("transaction")) {
        Transaction t;
        if (deleted) ok = del(QStringLiteral("DELETE FROM receipts WHERE transaction_id = ?")) && del(QStringLiteral("DELETE FROM transactions WHERE id = ?"));
        else ok = json::to(data, &t) && writeTransaction(t);
    } else if (entity == QLatin1String("recurring")) {
        Recurring r;
        ok = deleted ? del(QStringLiteral("DELETE FROM recurring WHERE id = ?")) : (json::to(data, &r) && writeRecurring(r));
    } else if (entity == QLatin1String("account")) {
        Account a;
        ok = deleted ? del(QStringLiteral("DELETE FROM accounts WHERE id = ?")) : (json::to(data, &a) && writeAccount(a));
    } else if (entity == QLatin1String("rule")) {
        ImportRule r;
        ok = deleted ? del(QStringLiteral("DELETE FROM import_rules WHERE id = ?")) : (json::to(data, &r) && writeImportRule(r));
    } else if (entity == QLatin1String("receipt")) {
        if (deleted) {
            ok = del(QStringLiteral("DELETE FROM receipts WHERE id = ?"));
        } else {
            Receipt r;
            if (!json::receiptFromMeta(data, &r)) return ApplyResult::Skipped;
            if (!loadReceipt || !loadReceipt(&r.data)) return ApplyResult::NotReady;
            r.size = r.data.size();
            ok = writeReceipt(r);
        }
    } else {
        return ApplyResult::Skipped; // unknown kind from a newer version
    }
    if (!ok) return ApplyResult::Skipped;
    QSqlQuery v(m_db);
    v.prepare(QStringLiteral("INSERT OR REPLACE INTO sync_versions(entity, id, ts, device) VALUES(?,?,?,?)"));
    v.addBindValue(entity);
    v.addBindValue(id);
    v.addBindValue(ts);
    v.addBindValue(device);
    v.exec();
    return ApplyResult::Applied;
}

bool Store::wipeForJoin(QString *error)
{
    const bool tracking = m_tracking;
    m_tracking = false;
    m_db.transaction();
    const bool ok = clearTables() && exec(QStringLiteral("DELETE FROM sync_outbox")) && exec(QStringLiteral("DELETE FROM sync_versions")) &&
                    exec(QStringLiteral("DELETE FROM sync_files"));
    if (!ok || !m_db.commit()) {
        if (error) *error = m_db.lastError().text();
        m_db.rollback();
        m_tracking = tracking;
        return false;
    }
    m_tracking = tracking;
    m_autoPosted.clear();
    loadCache();
    emit dataChanged();
    return true;
}

QJsonObject Store::syncConfig() const
{
    return QJsonDocument::fromJson(localValue(QStringLiteral("sync")).toUtf8()).object();
}

void Store::setSyncConfig(const QJsonObject &cfg)
{
    setLocalValue(QStringLiteral("sync"), cfg.isEmpty() ? QString() : QString::fromUtf8(QJsonDocument(cfg).toJson(QJsonDocument::Compact)));
    m_tracking = !cfg.value(QStringLiteral("folder")).toString().isEmpty();
}

bool Store::isFileApplied(const QString &name) const
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT 1 FROM sync_files WHERE name = ?"));
    q.addBindValue(name);
    return q.exec() && q.next();
}

void Store::markFileApplied(const QString &name)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("INSERT OR IGNORE INTO sync_files(name) VALUES(?)"));
    q.addBindValue(name);
    q.exec();
}
