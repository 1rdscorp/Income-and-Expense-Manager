#pragma once

#include <QByteArray>
#include <QDate>
#include <QString>
#include <QVector>

enum class TxType { Income, Expense };

inline QString typeToString(TxType t) { return t == TxType::Income ? QStringLiteral("income") : QStringLiteral("expense"); }
inline TxType typeFromString(const QString &s) { return s == QLatin1String("income") ? TxType::Income : TxType::Expense; }
inline QString typeLabel(TxType t) { return t == TxType::Income ? QStringLiteral("Income") : QStringLiteral("Expense"); }

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

    qint64 signedAmount() const { return type == TxType::Income ? amount : -amount; }
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
    QString exportedAt;
};

struct MergeStats {
    int categories = 0;
    int transactions = 0;
    int receipts = 0;
};
