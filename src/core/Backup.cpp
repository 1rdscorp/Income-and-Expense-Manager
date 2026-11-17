#include "Backup.h"
#include "Defaults.h"
#include "Json.h"
#include "Store.h"
#include "Util.h"
#include "Xlsx.h"

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

using json::clip;

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

    QJsonArray cats, txs, rules, accounts, importRules;
    for (const auto &c : store.categories()) cats.append(json::from(c));
    for (const auto &t : store.transactions()) txs.append(json::from(t));
    for (const auto &r : store.recurring()) rules.append(json::from(r));
    for (const auto &acc : store.accounts()) accounts.append(json::from(acc));
    for (const auto &ir : store.importRules()) importRules.append(json::from(ir));
    root[QStringLiteral("categories")] = cats;
    root[QStringLiteral("transactions")] = txs;
    root[QStringLiteral("recurring")] = rules;
    root[QStringLiteral("accounts")] = accounts;
    root[QStringLiteral("importRules")] = importRules;

    QJsonArray recs;
    if (includeReceipts) {
        for (const auto &r : store.allReceipts()) {
            QJsonObject o = json::receiptMeta(r);
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
        Category c;
        if (json::to(v.toObject(), &c)) d.categories.push_back(c);
    }
    for (const QJsonValue &v : root.value(QStringLiteral("transactions")).toArray()) {
        Transaction t;
        if (json::to(v.toObject(), &t)) d.transactions.push_back(t);
    }
    // Recurring rules, accounts and import rules are absent in older and web-app backups
    for (const QJsonValue &v : root.value(QStringLiteral("recurring")).toArray()) {
        Recurring r;
        if (json::to(v.toObject(), &r)) d.recurring.push_back(r);
    }
    for (const QJsonValue &v : root.value(QStringLiteral("accounts")).toArray()) {
        Account acc;
        if (json::to(v.toObject(), &acc)) d.accounts.push_back(acc);
    }
    for (const QJsonValue &v : root.value(QStringLiteral("importRules")).toArray()) {
        ImportRule ir;
        if (json::to(v.toObject(), &ir)) d.importRules.push_back(ir);
    }

    for (const QJsonValue &v : root.value(QStringLiteral("receipts")).toArray()) {
        const QJsonObject o = v.toObject();
        Receipt r;
        if (!json::receiptFromMeta(o, &r)) continue;
        if (!decodeDataUrl(o.value(QStringLiteral("data")).toString(), &r.data, &r.mime)) {
            if (error) *error = QStringLiteral("A receipt in the backup is corrupted.");
            return false;
        }
        r.size = r.data.size();
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

QByteArray transactionsXlsx(const Store &store, const QVector<Transaction> &list, const QString &title)
{
    xlsx::Workbook wb(util::currencySymbol().trimmed(), util::currencyDecimals());
    xlsx::Sheet &sh = wb.addSheet(QStringLiteral("Transactions"));
    xlsx::Cell t = xlsx::Cell::str(QStringLiteral("%1 — %2").arg(store.settings().businessName, title.isEmpty() ? QStringLiteral("Transactions") : title));
    t.title = true;
    sh.addRow({t});
    QVector<Transaction> sorted = list;
    std::stable_sort(sorted.begin(), sorted.end(), [](const Transaction &a, const Transaction &b) { return a.date < b.date; });
    Totals tot = Store::totals(sorted);
    xlsx::Cell sub = xlsx::Cell::str(QStringLiteral("%1 transactions · income %2 · expenses %3 · net %4")
                                         .arg(tot.count)
                                         .arg(util::money(tot.income), util::money(tot.expense), util::signedMoney(tot.net())));
    sub.muted = true;
    sh.addRow({sub});
    const QStringList heads = {QStringLiteral("Date"), QStringLiteral("Type"), QStringLiteral("Category"), QStringLiteral("Description"),
                               QStringLiteral("Vendor / Payee"), QStringLiteral("Payment method"), QStringLiteral("Amount"),
                               QStringLiteral("Signed amount"), QStringLiteral("Receipts"), QStringLiteral("Recurring"), QStringLiteral("Notes")};
    QVector<xlsx::Cell> head;
    for (const auto &h : heads) {
        xlsx::Cell c = xlsx::Cell::str(h);
        c.bold = true;
        c.fill = xlsx::Fill::Header;
        if (h == QLatin1String("Amount") || h == QLatin1String("Signed amount") || h == QLatin1String("Receipts")) c.align = xlsx::Align::Right;
        head << c;
    }
    sh.addRow(head);
    sh.frozenRows = 3;
    sh.autoFilterRow = 2;
    for (const auto &x : sorted) {
        sh.addRow({xlsx::Cell::day(x.date), xlsx::Cell::str(typeLabel(x.type)), xlsx::Cell::str(store.categoryOrUnknown(x.categoryId).name),
                   xlsx::Cell::str(x.description), xlsx::Cell::str(x.vendor), xlsx::Cell::str(x.paymentMethod), xlsx::Cell::money(x.amount),
                   xlsx::Cell::money(x.signedAmount()), xlsx::Cell::num(store.receiptCount(x.id), xlsx::Format::Integer),
                   xlsx::Cell::str(x.recurringId.isEmpty() ? QString() : QStringLiteral("Yes")), xlsx::Cell::str(x.notes)});
    }
    sh.autoFilterLastRow = sh.rows.size() - 1;
    QVector<xlsx::Cell> total{xlsx::Cell::str(QStringLiteral("Total")), xlsx::Cell(), xlsx::Cell(), xlsx::Cell(), xlsx::Cell(), xlsx::Cell(),
                              xlsx::Cell(), xlsx::Cell::money(tot.net())};
    for (auto &c : total) {
        c.bold = true;
        c.fill = xlsx::Fill::Total;
    }
    sh.addRow(total);
    sh.columnWidths = {13, 10, 26, 30, 22, 16, 14, 15, 10, 10, 40};
    return wb.toBytes();
}

bool exportTransactions(const Store &store, const QVector<Transaction> &list, const QString &path, const QString &title, QString *error)
{
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        if (error) *error = f.errorString();
        return false;
    }
    if (path.endsWith(QLatin1String(".xlsx"), Qt::CaseInsensitive)) f.write(transactionsXlsx(store, list, title));
    else f.write(transactionsCsv(store, list).toUtf8());
    if (!f.commit()) {
        if (error) *error = f.errorString();
        return false;
    }
    return true;
}

} // namespace backup
