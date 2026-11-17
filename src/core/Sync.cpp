#include "Sync.h"
#include "Backup.h"
#include "Store.h"
#include "Util.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSysInfo>
#include <algorithm>

namespace sync {

namespace {

const char *kManifest = "ledgerly-shared.json";

QString path(const QString &folder, const QString &rel) { return QDir(folder).filePath(rel); }

QJsonObject readJson(const QString &file, bool *ok = nullptr)
{
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly)) {
        if (ok) *ok = false;
        return {};
    }
    QJsonParseError pe;
    const QJsonDocument d = QJsonDocument::fromJson(f.readAll(), &pe);
    if (ok) *ok = pe.error == QJsonParseError::NoError && d.isObject();
    return d.object();
}

bool writeFile(const QString &file, const QByteArray &data, QString *error)
{
    QDir().mkpath(QFileInfo(file).absolutePath());
    QSaveFile f(file);
    if (!f.open(QIODevice::WriteOnly)) {
        if (error) *error = f.errorString();
        return false;
    }
    f.write(data);
    if (!f.commit()) {
        if (error) *error = f.errorString();
        return false;
    }
    return true;
}

const QRegularExpression &changeFileRe()
{
    static const QRegularExpression re(QStringLiteral("^(\\d{8})\\.json$"));
    return re;
}

int highestSeq(const QString &dir)
{
    int best = 0;
    for (const QString &f : QDir(dir).entryList({QStringLiteral("*.json")}, QDir::Files)) {
        const auto m = changeFileRe().match(f);
        if (m.hasMatch()) best = std::max(best, m.captured(1).toInt());
    }
    return best;
}

QString receiptFile(const QString &folder, const QString &id)
{
    QString safe = id;
    safe.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_-]")), QStringLiteral("_"));
    return path(folder, QStringLiteral("receipts/") + safe);
}

void writeDeviceInfo(const Store &store, const QString &folder)
{
    QJsonObject o;
    o[QStringLiteral("name")] = computerName();
    o[QStringLiteral("lastSync")] = util::nowIso();
    o[QStringLiteral("app")] = QStringLiteral(LEDGERLY_VERSION);
    writeFile(path(folder, QStringLiteral("devices/%1.json").arg(store.deviceId())), QJsonDocument(o).toJson(QJsonDocument::Indented), nullptr);
}

/* Applies every journal file not applied yet (including this computer's own
 * files from before a re-join). Returns the number of changes applied. */
int importAll(Store &store, const QString &folder, QString *error)
{
    const QString changesDir = path(folder, QStringLiteral("changes"));
    const QStringList devices = QDir(changesDir).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    struct Pending {
        QString key;
        QString file;
    };
    QVector<Pending> files;
    for (const QString &dev : devices) {
        const QString dir = QDir(changesDir).filePath(dev);
        for (const QString &f : QDir(dir).entryList({QStringLiteral("*.json")}, QDir::Files, QDir::Name)) {
            if (!changeFileRe().match(f).hasMatch()) continue;
            const QString key = dev + QLatin1Char('/') + f;
            if (!store.isFileApplied(key)) files.push_back({key, QDir(dir).filePath(f)});
        }
    }
    if (files.isEmpty()) return 0;

    int applied = 0;
    store.beginRemoteBatch();
    for (const auto &p : files) {
        bool ok = false;
        const QJsonObject root = readJson(p.file, &ok);
        if (!ok || root.value(QStringLiteral("app")).toString() != QLatin1String("ledgerly-sync")) continue; // partly synced: retry later
        bool complete = true;
        for (const QJsonValue &v : root.value(QStringLiteral("changes")).toArray()) {
            const QJsonObject c = v.toObject();
            const QString entity = c.value(QStringLiteral("entity")).toString();
            const QString id = c.value(QStringLiteral("id")).toString();
            const qint64 ts = static_cast<qint64>(c.value(QStringLiteral("ts")).toDouble());
            const QString device = c.value(QStringLiteral("device")).toString();
            if (entity.isEmpty() || id.isEmpty()) continue;
            auto loadReceipt = [&](QByteArray *data) {
                QFile f(receiptFile(folder, id));
                if (!f.open(QIODevice::ReadOnly)) return false;
                *data = f.readAll();
                const qint64 expected = static_cast<qint64>(c.value(QStringLiteral("data")).toObject().value(QStringLiteral("size")).toDouble());
                return expected <= 0 || data->size() == expected; // still uploading if sizes differ
            };
            const auto r = store.applyRemote(entity, id, ts, device, c.value(QStringLiteral("deleted")).toBool(),
                                             c.value(QStringLiteral("data")).toObject(), loadReceipt);
            if (r == Store::ApplyResult::Applied) ++applied;
            else if (r == Store::ApplyResult::NotReady) complete = false;
        }
        if (complete) store.markFileApplied(p.key);
    }
    store.endRemoteBatch(applied > 0);
    Q_UNUSED(error);
    return applied;
}

/* Writes all pending local changes as new journal files. */
int exportAll(Store &store, const QString &folder, QString *error)
{
    QJsonObject cfg = store.syncConfig();
    int seq = cfg.value(QStringLiteral("seq")).toInt();
    const QString ownDir = path(folder, QStringLiteral("changes/") + store.deviceId());
    seq = std::max(seq, highestSeq(ownDir));
    int written = 0;
    for (;;) {
        const auto batch = store.pendingChanges(1000);
        if (batch.isEmpty()) break;
        QJsonArray arr;
        for (const auto &ch : batch) {
            bool exists = false;
            const QJsonObject data = store.recordJson(ch.entity, ch.id, &exists);
            QJsonObject o;
            o[QStringLiteral("entity")] = ch.entity;
            o[QStringLiteral("id")] = ch.id;
            o[QStringLiteral("ts")] = static_cast<double>(ch.ts);
            o[QStringLiteral("device")] = store.deviceId();
            o[QStringLiteral("deleted")] = !exists;
            if (exists) o[QStringLiteral("data")] = data;
            if (exists && ch.entity == QLatin1String("receipt")) {
                const QString rf = receiptFile(folder, ch.id);
                if (!QFile::exists(rf) && !writeFile(rf, store.receiptData(ch.id), error)) return -1;
            }
            arr.append(o);
        }
        QJsonObject root;
        root[QStringLiteral("app")] = QStringLiteral("ledgerly-sync");
        root[QStringLiteral("version")] = 1;
        root[QStringLiteral("device")] = store.deviceId();
        root[QStringLiteral("computer")] = computerName();
        root[QStringLiteral("seq")] = seq + 1;
        root[QStringLiteral("createdAt")] = util::nowIso();
        root[QStringLiteral("changes")] = arr;
        const QString name = QStringLiteral("%1.json").arg(seq + 1, 8, 10, QLatin1Char('0'));
        if (!writeFile(QDir(ownDir).filePath(name), QJsonDocument(root).toJson(QJsonDocument::Compact), error)) return -1;
        ++seq;
        store.markFileApplied(store.deviceId() + QLatin1Char('/') + name); // never re-import our own file
        store.clearPendingChanges(batch);
        cfg[QStringLiteral("seq")] = seq;
        store.setSyncConfig(cfg);
        written += static_cast<int>(batch.size());
    }
    return written;
}

} // namespace

QString computerName()
{
    const QString n = QSysInfo::machineHostName();
    return n.isEmpty() ? QStringLiteral("Computer") : n;
}

FolderInfo inspect(const QString &folder)
{
    FolderInfo info;
    const QFileInfo fi(folder);
    info.exists = fi.exists() && fi.isDir();
    if (!info.exists) {
        info.error = QStringLiteral("The folder does not exist or is not available.");
        return info;
    }
    bool ok = false;
    const QJsonObject m = readJson(path(folder, QString::fromLatin1(kManifest)), &ok);
    if (ok && m.value(QStringLiteral("app")).toString() == QLatin1String("ledgerly-shared")) {
        info.hasLedger = true;
        info.ledgerId = m.value(QStringLiteral("ledgerId")).toString();
        info.createdBy = m.value(QStringLiteral("createdBy")).toString();
        for (const QString &f : QDir(path(folder, QStringLiteral("devices"))).entryList({QStringLiteral("*.json")}, QDir::Files)) {
            const QString name = readJson(path(folder, QStringLiteral("devices/") + f)).value(QStringLiteral("name")).toString();
            if (!name.isEmpty()) info.computers << name;
        }
    }
    return info;
}

bool isConnected(const Store &store) { return !store.syncConfig().value(QStringLiteral("folder")).toString().isEmpty(); }
QString folder(const Store &store) { return store.syncConfig().value(QStringLiteral("folder")).toString(); }

QDateTime lastSync(const Store &store)
{
    return QDateTime::fromString(store.syncConfig().value(QStringLiteral("lastSync")).toString(), Qt::ISODateWithMs);
}

QStringList computers(const Store &store)
{
    QStringList out;
    const QString f = folder(store);
    if (f.isEmpty()) return out;
    for (const QString &file : QDir(path(f, QStringLiteral("devices"))).entryList({QStringLiteral("*.json")}, QDir::Files)) {
        if (file == store.deviceId() + QStringLiteral(".json")) continue;
        const QJsonObject o = readJson(path(f, QStringLiteral("devices/") + file));
        const QString name = o.value(QStringLiteral("name")).toString();
        const QString when = util::fmtDateTime(o.value(QStringLiteral("lastSync")).toString());
        if (!name.isEmpty()) out << QStringLiteral("%1 (last synced %2)").arg(name, when);
    }
    return out;
}

bool connect(Store &store, const QString &folderPath, JoinMode mode, QString *error)
{
    const FolderInfo info = inspect(folderPath);
    if (!info.exists) {
        if (error) *error = info.error;
        return false;
    }
    const QString folder = QDir(folderPath).absolutePath();
    QJsonObject cfg;
    cfg[QStringLiteral("folder")] = folder;

    if (mode == JoinMode::Create) {
        if (info.hasLedger) {
            if (error) *error = QStringLiteral("This folder already contains shared Ledgerly data. Join it instead.");
            return false;
        }
        QJsonObject m;
        m[QStringLiteral("app")] = QStringLiteral("ledgerly-shared");
        m[QStringLiteral("version")] = 1;
        m[QStringLiteral("ledgerId")] = util::newId();
        m[QStringLiteral("createdAt")] = util::nowIso();
        m[QStringLiteral("createdBy")] = computerName();
        if (!writeFile(path(folder, QString::fromLatin1(kManifest)), QJsonDocument(m).toJson(QJsonDocument::Indented), error)) return false;
        QDir().mkpath(path(folder, QStringLiteral("changes")));
        QDir().mkpath(path(folder, QStringLiteral("receipts")));
        QDir().mkpath(path(folder, QStringLiteral("devices")));
        cfg[QStringLiteral("ledgerId")] = m.value(QStringLiteral("ledgerId"));
        cfg[QStringLiteral("seq")] = 0;
        store.setSyncConfig(cfg);
        store.trackEverything(); // first upload: everything on this computer
        const Result r = syncNow(store);
        if (!r.ok && error) *error = r.error;
        return r.ok;
    }

    if (!info.hasLedger) {
        if (error) *error = QStringLiteral("This folder doesn't contain shared Ledgerly data yet.");
        return false;
    }
    BackupData mine;
    if (mode == JoinMode::Merge) {
        QString perr;
        if (!backup::parse(backup::toJson(store, true), &mine, &perr)) {
            if (error) *error = perr;
            return false;
        }
    }
    store.setSyncConfig({});
    if (!store.wipeForJoin(error)) return false;
    cfg[QStringLiteral("ledgerId")] = info.ledgerId;
    cfg[QStringLiteral("seq")] = highestSeq(path(folder, QStringLiteral("changes/") + store.deviceId()));
    store.setSyncConfig(cfg);
    Result r = syncNow(store);
    if (!r.ok) {
        if (error) *error = r.error;
        return false;
    }
    if (mode == JoinMode::Merge) {
        MergeStats st;
        if (!store.merge(mine, &st, error)) return false;
        r = syncNow(store);
        if (!r.ok && error) *error = r.error;
        return r.ok;
    }
    return true;
}

void disconnect(Store &store)
{
    store.setSyncConfig({});
    store.clearPendingChanges(store.pendingChanges(1000000));
}

static Result failWith(Store &store, const QString &msg)
{
    Result res;
    res.ok = false;
    res.error = msg;
    QJsonObject cfg = store.syncConfig();
    if (!cfg.isEmpty()) {
        cfg[QStringLiteral("lastError")] = msg;
        store.setSyncConfig(cfg);
    }
    return res;
}

QString lastError(const Store &store) { return store.syncConfig().value(QStringLiteral("lastError")).toString(); }

Result syncNow(Store &store)
{
    Result res;
    QJsonObject cfg = store.syncConfig();
    const QString folder = cfg.value(QStringLiteral("folder")).toString();
    if (folder.isEmpty()) {
        res.ok = false;
        res.error = QStringLiteral("Not connected to a shared folder.");
        return res;
    }
    const FolderInfo info = inspect(folder);
    if (!info.exists)
        return failWith(store, QStringLiteral("The shared folder isn't available right now (%1). Your changes are kept and will be shared later.")
                                   .arg(QDir::toNativeSeparators(folder)));
    if (!info.hasLedger || info.ledgerId != cfg.value(QStringLiteral("ledgerId")).toString())
        return failWith(store, QStringLiteral("The shared folder no longer contains this ledger. Reconnect it in Backup & Sync."));

    QString err;
    res.imported = importAll(store, folder, &err);
    store.processRecurring(); // payments other computers' rules make due
    const int exported = exportAll(store, folder, &err);
    if (exported < 0) return failWith(store, QStringLiteral("Couldn't write to the shared folder: %1").arg(err));
    res.exported = exported;
    writeDeviceInfo(store, folder);
    cfg = store.syncConfig();
    cfg[QStringLiteral("lastSync")] = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    cfg.remove(QStringLiteral("lastError"));
    store.setSyncConfig(cfg);
    return res;
}

} // namespace sync
