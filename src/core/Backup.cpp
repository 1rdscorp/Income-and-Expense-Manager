#include "Backup.h"
#include "Defaults.h"
#include "Store.h"
#include "Util.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QUrl>
#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace backup {

namespace {

QString clip(const QJsonValue &v, int max)
{
    QString s = v.isString() ? v.toString() : (v.isDouble() ? QString::number(v.toDouble()) : QString());
    return s.left(max);
}

bool decodeDataUrl(const QString &url, QByteArray *data, QString *mime)
{
    static const QRegularExpression re(QStringLiteral("^data:([^;,]*)(;base64)?,"));
    const auto m = re.match(url);
    if (!m.hasMatch()) return false;
    *mime = m.captured(1).isEmpty() ? QStringLiteral("application/octet-stream") : m.captured(1);
    const QString payload = url.mid(m.capturedEnd());
    if (m.captured(2).isEmpty()) {
        *data = QUrl::fromPercentEncoding(payload.toUtf8()).toUtf8();
        return true;
    }
    const auto res = QByteArray::fromBase64Encoding(payload.toLatin1());
    if (!res) return false;
    *data = res.decoded;
    return true;
}

} // namespace

QByteArray toJson(const Store &store, bool includeReceipts, int *receiptCount)
{
    QJsonObject root;
    root[QStringLiteral("app")] = QStringLiteral("ledgerly");
    root[QStringLiteral("version")] = 1;
    root[QStringLiteral("exportedAt")] = util::nowIso();
    root[QStringLiteral("includesReceipts")] = includeReceipts;

    const Settings &s = store.settings();
    QJsonObject so;
    so[QStringLiteral("businessName")] = s.businessName;
    so[QStringLiteral("currency")] = s.currency;
    so[QStringLiteral("theme")] = s.theme;
    so[QStringLiteral("compressReceipts")] = s.compressReceipts;
    so[QStringLiteral("backupReminderDays")] = s.backupReminderDays;
    root[QStringLiteral("settings")] = so;

    QJsonArray cats;
    for (const auto &c : store.categories()) {
        QJsonObject o;
        o[QStringLiteral("id")] = c.id;
        o[QStringLiteral("name")] = c.name;
        o[QStringLiteral("type")] = typeToString(c.type);
        o[QStringLiteral("icon")] = c.icon;
        o[QStringLiteral("budget")] = static_cast<double>(c.budget);
        o[QStringLiteral("createdAt")] = c.createdAt;
        cats.append(o);
    }
    root[QStringLiteral("categories")] = cats;

    QJsonArray txs;
    for (const auto &t : store.transactions()) {
        QJsonObject o;
        o[QStringLiteral("id")] = t.id;
        o[QStringLiteral("type")] = typeToString(t.type);
        o[QStringLiteral("categoryId")] = t.categoryId;
        o[QStringLiteral("amount")] = static_cast<double>(t.amount);
        o[QStringLiteral("date")] = t.date.toString(Qt::ISODate);
        o[QStringLiteral("description")] = t.description;
        o[QStringLiteral("vendor")] = t.vendor;
        o[QStringLiteral("paymentMethod")] = t.paymentMethod;
        o[QStringLiteral("notes")] = t.notes;
        o[QStringLiteral("createdAt")] = t.createdAt;
        o[QStringLiteral("updatedAt")] = t.updatedAt;
        txs.append(o);
    }
    root[QStringLiteral("transactions")] = txs;

    QJsonArray recs;
    if (includeReceipts) {
        for (const auto &r : store.allReceipts()) {
            QJsonObject o;
            o[QStringLiteral("id")] = r.id;
            o[QStringLiteral("transactionId")] = r.transactionId;
            o[QStringLiteral("name")] = r.name;
            o[QStringLiteral("mime")] = r.mime;
            o[QStringLiteral("size")] = static_cast<double>(r.data.size());
            o[QStringLiteral("createdAt")] = r.createdAt;
            o[QStringLiteral("data")] = QStringLiteral("data:%1;base64,%2").arg(r.mime, QString::fromLatin1(r.data.toBase64()));
            recs.append(o);
        }
    }
    root[QStringLiteral("receipts")] = recs;
    if (receiptCount) *receiptCount = static_cast<int>(recs.size());
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

bool parse(const QByteArray &json, BackupData *out, QString *error)
{
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject()) {
        if (error) *error = QStringLiteral("The file is not valid JSON (%1).").arg(pe.errorString());
        return false;
    }
    const QJsonObject root = doc.object();
    if (root.value(QStringLiteral("app")).toString() != QLatin1String("ledgerly")) {
        if (error) *error = QStringLiteral("This file is not a Ledgerly backup.");
        return false;
    }
    if (!root.value(QStringLiteral("categories")).isArray() || !root.value(QStringLiteral("transactions")).isArray()) {
        if (error) *error = QStringLiteral("The backup is missing categories or transactions.");
        return false;
    }

    BackupData d;
    d.exportedAt = root.value(QStringLiteral("exportedAt")).toString();

    const QJsonObject so = root.value(QStringLiteral("settings")).toObject();
    if (!so.value(QStringLiteral("businessName")).toString().isEmpty()) d.settings.businessName = clip(so.value(QStringLiteral("businessName")), 80);
    const QString cur = so.value(QStringLiteral("currency")).toString();
    for (const auto &c : defaults::currencies())
        if (c.code == cur) d.settings.currency = cur;
    const QString theme = so.value(QStringLiteral("theme")).toString();
    if (theme == QLatin1String("system") || theme == QLatin1String("light") || theme == QLatin1String("dark")) d.settings.theme = theme;
    if (so.value(QStringLiteral("compressReceipts")).isBool()) d.settings.compressReceipts = so.value(QStringLiteral("compressReceipts")).toBool();
    const int rem = so.value(QStringLiteral("backupReminderDays")).toInt(-1);
    if (rem == 0 || rem == 1 || rem == 7 || rem == 30) d.settings.backupReminderDays = rem;

    for (const QJsonValue &v : root.value(QStringLiteral("categories")).toArray()) {
        const QJsonObject o = v.toObject();
        Category c;
        c.id = clip(o.value(QStringLiteral("id")), 100);
        c.name = clip(o.value(QStringLiteral("name")), 60).simplified();
        if (c.id.isEmpty() || c.name.isEmpty()) continue;
        c.type = typeFromString(o.value(QStringLiteral("type")).toString());
        c.icon = clip(o.value(QStringLiteral("icon")), 16);
        if (c.icon.isEmpty()) c.icon = QStringLiteral("🏷");
        const double b = o.value(QStringLiteral("budget")).toDouble();
        c.budget = (b > 0 && c.type == TxType::Expense) ? static_cast<qint64>(b) : 0;
        c.createdAt = clip(o.value(QStringLiteral("createdAt")), 40);
        if (c.createdAt.isEmpty()) c.createdAt = util::nowIso();
        d.categories.push_back(c);
    }

    for (const QJsonValue &v : root.value(QStringLiteral("transactions")).toArray()) {
        const QJsonObject o = v.toObject();
        Transaction t;
        t.id = clip(o.value(QStringLiteral("id")), 100);
        t.date = QDate::fromString(o.value(QStringLiteral("date")).toString(), Qt::ISODate);
        const QJsonValue amt = o.value(QStringLiteral("amount"));
        if (t.id.isEmpty() || !t.date.isValid() || !amt.isDouble()) continue;
        t.amount = std::llabs(static_cast<qint64>(std::llround(amt.toDouble())));
        t.type = typeFromString(o.value(QStringLiteral("type")).toString());
        t.categoryId = clip(o.value(QStringLiteral("categoryId")), 100);
        t.description = clip(o.value(QStringLiteral("description")), 200);
        t.vendor = clip(o.value(QStringLiteral("vendor")), 120);
        t.paymentMethod = clip(o.value(QStringLiteral("paymentMethod")), 60);
        t.notes = clip(o.value(QStringLiteral("notes")), 2000);
        t.createdAt = clip(o.value(QStringLiteral("createdAt")), 40);
        t.updatedAt = clip(o.value(QStringLiteral("updatedAt")), 40);
        d.transactions.push_back(t);
    }

    for (const QJsonValue &v : root.value(QStringLiteral("receipts")).toArray()) {
        const QJsonObject o = v.toObject();
        Receipt r;
        r.id = clip(o.value(QStringLiteral("id")), 100);
        r.transactionId = clip(o.value(QStringLiteral("transactionId")), 100);
        if (r.id.isEmpty() || r.transactionId.isEmpty()) continue;
        if (!decodeDataUrl(o.value(QStringLiteral("data")).toString(), &r.data, &r.mime)) {
            if (error) *error = QStringLiteral("A receipt in the backup is corrupted.");
            return false;
        }
        r.name = clip(o.value(QStringLiteral("name")), 200);
        if (r.name.isEmpty()) r.name = QStringLiteral("receipt");
        r.size = r.data.size();
        r.createdAt = clip(o.value(QStringLiteral("createdAt")), 40);
        if (r.createdAt.isEmpty()) r.createdAt = util::nowIso();
        d.receipts.push_back(r);
    }

    *out = d;
    return true;
}

QString defaultFileName(const Store &store, bool includeReceipts)
{
    return QStringLiteral("%1-backup-%2%3.json")
        .arg(util::slug(store.settings().businessName), QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd-HHmm")),
             includeReceipts ? QString() : QStringLiteral("-no-receipts"));
}

bool writeFile(Store &store, const QString &path, bool includeReceipts, QString *error, int *receiptCount)
{
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        if (error) *error = f.errorString();
        return false;
    }
    f.write(toJson(store, includeReceipts, receiptCount));
    if (!f.commit()) {
        if (error) *error = f.errorString();
        return false;
    }
    Settings s = store.settings();
    s.lastBackupAt = util::nowIso();
    store.saveSettings(s);
    return true;
}

bool writeAutoBackup(Store &store, const QString &dirPath, int keep, QString *error, QString *writtenPath)
{
    QDir dir(dirPath);
    if (!dir.exists() && !QDir().mkpath(dirPath)) {
        if (error) *error = QStringLiteral("Cannot create folder %1").arg(dirPath);
        return false;
    }
    const QString name = QStringLiteral("ledgerly-auto-%1.json").arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd-HHmmss")));
    const QString path = dir.filePath(name);
    if (!writeFile(store, path, true, error)) return false;
    if (writtenPath) *writtenPath = path;
    QStringList files = dir.entryList({QStringLiteral("ledgerly-auto-*.json")}, QDir::Files, QDir::Name);
    keep = std::max(1, keep);
    while (files.size() > keep) dir.remove(files.takeFirst());
    return true;
}

QString transactionsCsv(const Store &store, const QVector<Transaction> &list)
{
    QVector<QStringList> rows;
    rows << QStringList{QStringLiteral("Date"), QStringLiteral("Type"), QStringLiteral("Category"), QStringLiteral("Description"),
                        QStringLiteral("Vendor / Payee"), QStringLiteral("Payment method"), QStringLiteral("Amount"),
                        QStringLiteral("Signed amount"), QStringLiteral("Receipts"), QStringLiteral("Notes")};
    QVector<Transaction> sorted = list;
    std::stable_sort(sorted.begin(), sorted.end(), [](const Transaction &a, const Transaction &b) { return a.date < b.date; });
    for (const auto &t : sorted) {
        rows << QStringList{t.date.toString(Qt::ISODate), typeLabel(t.type), store.categoryOrUnknown(t.categoryId).name, t.description,
                            t.vendor, t.paymentMethod, util::amountPlain(t.amount), util::amountPlain(t.signedAmount()),
                            QString::number(store.receiptCount(t.id)), t.notes};
    }
    return util::toCsv(rows);
}

} // namespace backup
