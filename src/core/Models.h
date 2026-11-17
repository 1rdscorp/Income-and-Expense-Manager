#pragma once

#include <QByteArray>
#include <QDate>
#include <QString>
#include <QVector>

enum class TxType { Income, Expense };

inline QString typeToString(TxType t) { return t == TxType::Income ? QStringLiteral("income") : QStringLiteral("expense"); }
inline TxType typeFromString(const QString &s) { return s == QLatin1String("income") ? TxType::Income : TxType::Expense; }
inline QString typeLabel(TxType t) { return t == TxType::Income ? QStringLiteral("Income") : QStringLiteral("Expense"); }

enum class Frequency { Weekly, Monthly, Quarterly, Yearly };

inline QString frequencyToString(Frequency f)
{
    switch (f) {
    case Frequency::Weekly: return QStringLiteral("weekly");
    case Frequency::Quarterly: return QStringLiteral("quarterly");
    case Frequency::Yearly: return QStringLiteral("yearly");
    default: return QStringLiteral("monthly");
    }
}
inline Frequency frequencyFromString(const QString &s)
{
    if (s == QLatin1String("weekly")) return Frequency::Weekly;
    if (s == QLatin1String("quarterly")) return Frequency::Quarterly;
    if (s == QLatin1String("yearly")) return Frequency::Yearly;
    return Frequency::Monthly;
}
inline QString frequencyLabel(Frequency f)
{
    switch (f) {
    case Frequency::Weekly: return QStringLiteral("Weekly");
    case Frequency::Quarterly: return QStringLiteral("Every 3 months");
    case Frequency::Yearly: return QStringLiteral("Yearly");
    default: return QStringLiteral("Monthly");
    }
}

struct Category {
    QString id;
    QString name;
    TxType type = TxType::Expense;
    QString icon;
    qint64 budget = 0; // monthly budget in cents (expense categories only)
    QString createdAt;
};

struct Transaction {
    QString id;
    TxType type = TxType::Expense;
    QString categoryId;
    qint64 amount = 0; // always positive, in cents
    QDate date;
    QString description;
    QString vendor;
    QString paymentMethod;
    QString notes;
    QString createdAt;
    QString updatedAt;
    QString recurringId; // set when created by a recurring rule
    QString accountId;   // bank account / card it was imported from
    QString externalId;  // bank's transaction id (or a fingerprint) to skip duplicates on re-import

    qint64 signedAmount() const { return type == TxType::Income ? amount : -amount; }
};

/* A repeating transaction (rent, internet, salaries...). Occurrences are posted as
 * normal transactions on their due date. */
struct Recurring {
    QString id;
    TxType type = TxType::Expense;
    QString categoryId;
    qint64 amount = 0;
    QString description;
    QString vendor;
    QString paymentMethod;
    QString notes;
    Frequency frequency = Frequency::Monthly;
    int day = 1;        // day of month the payment falls on (clamped to short months)
    QDate startDate;    // first occurrence
    QDate endDate;      // invalid = no end
    QDate nextDate;     // next occurrence not yet recorded
    bool active = true; // paused rules record nothing
    QString createdAt;
    QString updatedAt;

    bool finished() const { return endDate.isValid() && nextDate.isValid() && nextDate > endDate; }
    QString title() const { return !description.isEmpty() ? description : vendor; }
};

/* A bank account or credit card that statements are imported from. */
struct Account {
    QString id;
    QString name;
    QString kind = QStringLiteral("bank"); // bank | card
    bool flipSigns = false;                // statements show money going out as positive numbers
    QString csvMapping;                    // remembered CSV column mapping (JSON)
    QString createdAt;
    QString updatedAt;
    bool isCard() const { return kind == QLatin1String("card"); }
};

/* "When the bank description contains <pattern>, use <category>" (or ignore the line). */
struct ImportRule {
    QString id;
    QString pattern;
    QString categoryId;
    bool ignore = false; // e.g. card payments / transfers between own accounts
    QString createdAt;
    QString updatedAt;
};

struct Receipt {
    QString id;
    QString transactionId;
    QString name;
    QString mime;
    qint64 size = 0;
    QByteArray data;
    QString createdAt;

    bool isImage() const { return mime.startsWith(QLatin1String("image/")); }
};

struct Settings {
    QString businessName = QStringLiteral("My Business");
    QString currency = QStringLiteral("USD");
    QString theme = QStringLiteral("system"); // system | light | dark
    bool compressReceipts = true;
    int backupReminderDays = 7; // 0 = off
    QString lastBackupAt;       // ISO timestamp
    QString autoBackupDir;      // empty = off
    int autoBackupKeep = 10;
};

struct Totals {
    qint64 income = 0;
    qint64 expense = 0;
    int count = 0;
    qint64 net() const { return income - expense; }
};

struct CategoryTotal {
    qint64 total = 0;
    int count = 0;
};

/* Everything that goes into / comes out of a backup file. */
struct BackupData {
    Settings settings;
    QVector<Category> categories;
    QVector<Transaction> transactions;
    QVector<Receipt> receipts;
    QVector<Recurring> recurring;
    QVector<Account> accounts;
    QVector<ImportRule> importRules;
    QString exportedAt;
};

struct MergeStats {
    int categories = 0;
    int transactions = 0;
    int receipts = 0;
    int recurring = 0;
};
