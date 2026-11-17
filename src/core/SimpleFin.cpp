#include "SimpleFin.h"
#include "Store.h"
#include "Util.h"

#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <algorithm>
#include <cmath>

namespace simplefin {

namespace {

const char *kKey = "simplefin";
constexpr int kMaxDaysPerRequest = 60; // keep each request's date range modest

QJsonObject config(const Store &store) { return QJsonDocument::fromJson(store.localValue(QString::fromLatin1(kKey)).toUtf8()).object(); }

void saveConfig(Store &store, const QJsonObject &cfg)
{
    store.setLocalValue(QString::fromLatin1(kKey), cfg.isEmpty() ? QString() : QString::fromUtf8(QJsonDocument(cfg).toJson(QJsonDocument::Compact)));
}

bool isLocalHost(const QUrl &u) { return u.host() == QLatin1String("127.0.0.1") || u.host() == QLatin1String("localhost"); }

// Only HTTPS is allowed (plain HTTP only for the local test server).
bool safeUrl(const QUrl &u) { return u.isValid() && (u.scheme() == QLatin1String("https") || (u.scheme() == QLatin1String("http") && isLocalHost(u))); }

struct HttpResult {
    int status = 0;
    QByteArray body;
    QString error;
};

HttpResult request(const QUrl &url, const QByteArray &method, int timeoutMs)
{
    HttpResult res;
    QNetworkAccessManager nam;
    QUrl target = url;
    QNetworkRequest req;
    if (!target.userName().isEmpty()) {
        const QByteArray creds = (QUrl::fromPercentEncoding(target.userName(QUrl::FullyEncoded).toUtf8()) + QLatin1Char(':') +
                                  QUrl::fromPercentEncoding(target.password(QUrl::FullyEncoded).toUtf8()))
                                     .toUtf8();
        req.setRawHeader("Authorization", "Basic " + creds.toBase64());
        target.setUserInfo(QString());
    }
    req.setUrl(target);
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Ledgerly/%1").arg(QStringLiteral(LEDGERLY_VERSION)));
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply *reply = nullptr;
    if (method == "POST") {
        req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/x-www-form-urlencoded"));
        reply = nam.post(req, QByteArray());
    } else {
        reply = nam.get(req);
    }
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    timer.start(timeoutMs);
    loop.exec();
    if (!reply->isFinished()) {
        reply->abort();
        res.error = QStringLiteral("The bank connection service didn't respond in time.");
        reply->deleteLater();
        return res;
    }
    res.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    res.body = reply->readAll();
    if (reply->error() != QNetworkReply::NoError && res.status == 0) res.error = reply->errorString();
    reply->deleteLater();
    return res;
}

qint64 parseMoney(const QJsonValue &v)
{
    const double d = v.isString() ? v.toString().toDouble() : v.toDouble();
    return static_cast<qint64>(std::llround(d * 100.0));
}

QDate fromEpoch(const QJsonValue &v)
{
    const qint64 secs = static_cast<qint64>(v.isString() ? v.toString().toDouble() : v.toDouble());
    return secs > 0 ? QDateTime::fromSecsSinceEpoch(secs).toLocalTime().date() : QDate();
}

QString accountLabel(const RemoteAccount &a) { return a.org.isEmpty() ? a.name : QStringLiteral("%1 %2").arg(a.org, a.name); }

bool looksLikeCard(const RemoteAccount &a)
{
    const QString n = a.name.toLower();
    for (const char *w : {"card", "visa", "mastercard", "amex", "american express", "discover", "credit"})
        if (n.contains(QLatin1String(w))) return true;
    return false;
}

} // namespace

bool parseAccountSet(const QByteArray &json, AccountSet *out, QString *error)
{
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject()) {
        if (error) *error = QStringLiteral("Unexpected response from the bank connection service.");
        return false;
    }
    const QJsonObject root = doc.object();
    AccountSet set;
    // v1: "errors": ["message", ...]   v2: "errlist": [{"msg": "...", ...}]
    for (const QJsonValue &e : root.value(QStringLiteral("errors")).toArray())
        if (!e.toString().isEmpty()) set.errors << e.toString();
    for (const QJsonValue &e : root.value(QStringLiteral("errlist")).toArray()) {
        const QString msg = e.toObject().value(QStringLiteral("msg")).toString();
        if (!msg.isEmpty()) set.errors << msg;
    }
    // v2 lists institutions separately ("connections") and links accounts by conn_id
    QHash<QString, QString> connNames;
    for (const QJsonValue &c : root.value(QStringLiteral("connections")).toArray()) {
        const QJsonObject o = c.toObject();
        connNames.insert(o.value(QStringLiteral("conn_id")).toString(), o.value(QStringLiteral("name")).toString());
    }
    for (const QJsonValue &v : root.value(QStringLiteral("accounts")).toArray()) {
        const QJsonObject o = v.toObject();
        RemoteAccount a;
        a.id = o.value(QStringLiteral("id")).toString();
        if (a.id.isEmpty()) continue;
        a.name = o.value(QStringLiteral("name")).toString().simplified();
        a.org = o.value(QStringLiteral("org")).toObject().value(QStringLiteral("name")).toString();
        if (a.org.isEmpty()) a.org = connNames.value(o.value(QStringLiteral("conn_id")).toString());
        a.currency = o.value(QStringLiteral("currency")).toString();
        a.balance = parseMoney(o.value(QStringLiteral("balance")));
        for (const QJsonValue &tv : o.value(QStringLiteral("transactions")).toArray()) {
            const QJsonObject t = tv.toObject();
            if (t.value(QStringLiteral("pending")).toBool(false)) continue; // wait until it posts
            bank::StatementLine l;
            l.date = fromEpoch(t.value(QStringLiteral("posted")));
            if (!l.date.isValid()) l.date = fromEpoch(t.value(QStringLiteral("transacted_at")));
            l.amount = parseMoney(t.value(QStringLiteral("amount")));
            const QString id = t.value(QStringLiteral("id")).toString();
            if (!l.date.isValid() || l.amount == 0 || id.isEmpty()) continue;
            const QString payee = t.value(QStringLiteral("payee")).toString().simplified();
            const QString desc = t.value(QStringLiteral("description")).toString().simplified();
            l.description = payee.isEmpty() ? desc : payee;
            l.memo = t.value(QStringLiteral("memo")).toString().simplified();
            if (!payee.isEmpty() && desc != payee && l.memo.isEmpty()) l.memo = desc;
            l.externalId = QStringLiteral("sfin:") + a.id + QLatin1Char(':') + id;
            a.lines << l;
        }
        set.accounts << a;
    }
    *out = set;
    return true;
}

bool claim(const QString &setupToken, QString *accessUrl, QString *error, int timeoutMs)
{
    const QByteArray decoded = QByteArray::fromBase64(setupToken.trimmed().toLatin1());
    const QUrl claimUrl(QString::fromUtf8(decoded).trimmed());
    if (!safeUrl(claimUrl) || claimUrl.host().isEmpty()) {
        if (error) *error = QStringLiteral("That doesn't look like a SimpleFIN setup token. Copy the whole token from SimpleFIN Bridge.");
        return false;
    }
    const HttpResult r = request(claimUrl, "POST", timeoutMs);
    if (!r.error.isEmpty()) {
        if (error) *error = r.error;
        return false;
    }
    if (r.status == 403) {
        if (error)
            *error = QStringLiteral("This setup token was already used or is invalid. If you didn't use it yourself, disable it on "
                                    "SimpleFIN Bridge, then create a new token and try again.");
        return false;
    }
    const QUrl access(QString::fromUtf8(r.body).trimmed());
    if (r.status < 200 || r.status >= 300 || !safeUrl(access) || access.userName().isEmpty()) {
        if (error) *error = QStringLiteral("The bank connection service returned an unexpected answer (HTTP %1).").arg(r.status);
        return false;
    }
    *accessUrl = access.toString(QUrl::FullyEncoded);
    return true;
}

bool fetch(const QString &accessUrl, const QDate &start, const QDate &end, bool balancesOnly, AccountSet *out, QString *error, int timeoutMs)
{
    QUrl url(accessUrl);
    if (!safeUrl(url)) {
        if (error) *error = QStringLiteral("The saved bank connection is invalid. Reconnect it.");
        return false;
    }
    QString path = url.path();
    if (!path.endsWith(QLatin1Char('/'))) path += QLatin1Char('/');
    url.setPath(path + QStringLiteral("accounts"));
    QUrlQuery q;
    if (start.isValid()) q.addQueryItem(QStringLiteral("start-date"), QString::number(QDateTime(start, QTime(0, 0)).toSecsSinceEpoch()));
    if (end.isValid()) q.addQueryItem(QStringLiteral("end-date"), QString::number(QDateTime(end.addDays(1), QTime(0, 0)).toSecsSinceEpoch()));
    if (balancesOnly) q.addQueryItem(QStringLiteral("balances-only"), QStringLiteral("1"));
    url.setQuery(q);
    const HttpResult r = request(url, "GET", timeoutMs);
    if (!r.error.isEmpty()) {
        if (error) *error = r.error;
        return false;
    }
    if (r.status == 403 || r.status == 401) {
        if (error) *error = QStringLiteral("Access was refused. The connection may have been disabled on SimpleFIN Bridge — reconnect it.");
        return false;
    }
    if (r.status == 402) {
        if (error) *error = QStringLiteral("SimpleFIN Bridge says a payment is needed on your account.");
        return false;
    }
    if (r.status < 200 || r.status >= 300) {
        if (error) *error = QStringLiteral("The bank connection service returned HTTP %1.").arg(r.status);
        return false;
    }
    return parseAccountSet(r.body, out, error);
}

bool isConnected(const Store &store) { return !config(store).value(QStringLiteral("accessUrl")).toString().isEmpty(); }
QDateTime lastDownload(const Store &store) { return QDateTime::fromString(config(store).value(QStringLiteral("lastDownload")).toString(), Qt::ISODate); }
QString lastError(const Store &store) { return config(store).value(QStringLiteral("lastError")).toString(); }

QStringList linkedAccountNames(const Store &store)
{
    QStringList out;
    const QJsonObject links = config(store).value(QStringLiteral("links")).toObject();
    for (auto it = links.begin(); it != links.end(); ++it)
        if (const Account *a = store.account(it.value().toString())) out << a->name;
    std::sort(out.begin(), out.end());
    return out;
}

static void linkAccounts(Store &store, QJsonObject &cfg, const AccountSet &set)
{
    QJsonObject links = cfg.value(QStringLiteral("links")).toObject();
    for (const RemoteAccount &ra : set.accounts) {
        if (store.account(links.value(ra.id).toString())) continue;
        // reuse an account with the same name, otherwise create one
        const QString label = accountLabel(ra);
        QString id;
        for (const auto &a : store.accounts())
            if (a.name.compare(label, Qt::CaseInsensitive) == 0) id = a.id;
        if (id.isEmpty()) {
            Account a;
            a.name = label;
            a.kind = looksLikeCard(ra) ? QStringLiteral("card") : QStringLiteral("bank");
            QString err;
            if (!store.saveAccount(a, &err, &id)) continue;
        }
        links.insert(ra.id, id);
    }
    cfg[QStringLiteral("links")] = links;
}

bool connect(Store &store, const QString &setupToken, QString *error)
{
    QString access;
    if (!claim(setupToken, &access, error)) return false;
    // The token is single-use: save the access URL immediately so it isn't lost.
    QJsonObject cfg;
    cfg[QStringLiteral("accessUrl")] = access;
    cfg[QStringLiteral("connectedAt")] = util::nowIso();
    saveConfig(store, cfg);
    AccountSet set;
    if (!fetch(access, QDate(), QDate(), true, &set, error)) return false;
    linkAccounts(store, cfg, set);
    saveConfig(store, cfg);
    return true;
}

void disconnect(Store &store) { saveConfig(store, {}); }

DownloadResult download(Store &store, bool force, int minHours)
{
    DownloadResult res;
    QJsonObject cfg = config(store);
    const QString access = cfg.value(QStringLiteral("accessUrl")).toString();
    if (access.isEmpty()) {
        res.ok = false;
        res.error = QStringLiteral("No bank connection.");
        return res;
    }
    const QDateTime last = QDateTime::fromString(cfg.value(QStringLiteral("lastDownload")).toString(), Qt::ISODate);
    if (!force && last.isValid() && last.secsTo(QDateTime::currentDateTimeUtc()) < static_cast<qint64>(minHours) * 3600) return res;

    // First download: the last 60 days. Later: from a few days before the last download
    // (late-posting transactions); anything already imported is skipped by its bank id.
    const QDate today = QDate::currentDate();
    QDate start = last.isValid() ? last.toLocalTime().date().addDays(-5) : today.addDays(-kMaxDaysPerRequest);
    start = std::max(start, today.addDays(-365));
    while (start <= today) {
        const QDate end = std::min(today, start.addDays(kMaxDaysPerRequest - 1));
        AccountSet set;
        QString err;
        if (!fetch(access, start, end, false, &set, &err)) {
            res.ok = false;
            res.error = err;
            cfg[QStringLiteral("lastError")] = err;
            saveConfig(store, cfg);
            return res;
        }
        for (const QString &m : set.errors)
            if (!res.messages.contains(m)) res.messages << m;
        linkAccounts(store, cfg, set);
        const QJsonObject links = cfg.value(QStringLiteral("links")).toObject();
        for (const RemoteAccount &ra : set.accounts) {
            const Account *acc = store.account(links.value(ra.id).toString());
            if (!acc || ra.lines.isEmpty()) continue;
            const auto items = bank::prepare(store, *acc, ra.lines);
            QString cerr;
            const int n = bank::commit(store, items, &cerr);
            if (n < 0) {
                res.ok = false;
                res.error = cerr;
                return res;
            }
            res.imported += n;
            for (const auto &p : items)
                if (!p.include && p.status != bank::Proposed::Status::Duplicate) ++res.skipped;
        }
        start = end.addDays(1);
    }
    cfg[QStringLiteral("lastDownload")] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    if (res.messages.isEmpty()) cfg.remove(QStringLiteral("lastError"));
    else cfg[QStringLiteral("lastError")] = res.messages.join(QStringLiteral(" "));
    saveConfig(store, cfg);
    return res;
}

} // namespace simplefin
