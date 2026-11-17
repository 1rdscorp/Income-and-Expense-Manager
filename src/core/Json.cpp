#include "Json.h"
#include "Util.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace json {

namespace {
inline QString k(const char *s) { return QString::fromLatin1(s); }
qint64 cents(const QJsonValue &v) { return std::llabs(static_cast<qint64>(std::llround(v.toDouble()))); }
QString dateStr(const QDate &d) { return d.isValid() ? d.toString(Qt::ISODate) : QString(); }
QDate dateOf(const QJsonValue &v) { return QDate::fromString(v.toString(), Qt::ISODate); }
} // namespace

QString clip(const QJsonValue &v, int max)
{
    QString s = v.isString() ? v.toString() : (v.isDouble() ? QString::number(v.toDouble()) : QString());
    return s.left(max);
}

QJsonObject from(const Category &c)
{
    QJsonObject o;
    o[k("id")] = c.id;
    o[k("name")] = c.name;
    o[k("type")] = typeToString(c.type);
    o[k("icon")] = c.icon;
    o[k("budget")] = static_cast<double>(c.budget);
    o[k("createdAt")] = c.createdAt;
    return o;
}

bool to(const QJsonObject &o, Category *c)
{
    c->id = clip(o.value(k("id")), 100);
    c->name = clip(o.value(k("name")), 60).simplified();
    if (c->id.isEmpty() || c->name.isEmpty()) return false;
    c->type = typeFromString(o.value(k("type")).toString());
    c->icon = clip(o.value(k("icon")), 16);
    if (c->icon.isEmpty()) c->icon = QStringLiteral("🏷");
    const double b = o.value(k("budget")).toDouble();
    c->budget = (b > 0 && c->type == TxType::Expense) ? static_cast<qint64>(b) : 0;
    c->createdAt = clip(o.value(k("createdAt")), 40);
    if (c->createdAt.isEmpty()) c->createdAt = util::nowIso();
    return true;
}

QJsonObject from(const Transaction &t)
{
    QJsonObject o;
    o[k("id")] = t.id;
    o[k("type")] = typeToString(t.type);
    o[k("categoryId")] = t.categoryId;
    o[k("amount")] = static_cast<double>(t.amount);
    o[k("date")] = dateStr(t.date);
    o[k("description")] = t.description;
    o[k("vendor")] = t.vendor;
    o[k("paymentMethod")] = t.paymentMethod;
    o[k("notes")] = t.notes;
    o[k("createdAt")] = t.createdAt;
    o[k("updatedAt")] = t.updatedAt;
    if (!t.recurringId.isEmpty()) o[k("recurringId")] = t.recurringId;
    if (!t.accountId.isEmpty()) o[k("accountId")] = t.accountId;
    if (!t.externalId.isEmpty()) o[k("externalId")] = t.externalId;
    return o;
}

bool to(const QJsonObject &o, Transaction *t)
{
    t->id = clip(o.value(k("id")), 100);
    t->date = dateOf(o.value(k("date")));
    const QJsonValue amt = o.value(k("amount"));
    if (t->id.isEmpty() || !t->date.isValid() || !amt.isDouble()) return false;
    t->amount = cents(amt);
    t->type = typeFromString(o.value(k("type")).toString());
    t->categoryId = clip(o.value(k("categoryId")), 100);
    t->description = clip(o.value(k("description")), 200);
    t->vendor = clip(o.value(k("vendor")), 120);
    t->paymentMethod = clip(o.value(k("paymentMethod")), 60);
    t->notes = clip(o.value(k("notes")), 2000);
    t->createdAt = clip(o.value(k("createdAt")), 40);
    t->updatedAt = clip(o.value(k("updatedAt")), 40);
    t->recurringId = clip(o.value(k("recurringId")), 100);
    t->accountId = clip(o.value(k("accountId")), 100);
    t->externalId = clip(o.value(k("externalId")), 200);
    return true;
}

QJsonObject from(const Recurring &r)
{
    QJsonObject o;
    o[k("id")] = r.id;
    o[k("type")] = typeToString(r.type);
    o[k("categoryId")] = r.categoryId;
    o[k("amount")] = static_cast<double>(r.amount);
    o[k("description")] = r.description;
    o[k("vendor")] = r.vendor;
    o[k("paymentMethod")] = r.paymentMethod;
    o[k("notes")] = r.notes;
    o[k("frequency")] = frequencyToString(r.frequency);
    o[k("day")] = r.day;
    o[k("startDate")] = dateStr(r.startDate);
    o[k("endDate")] = dateStr(r.endDate);
    o[k("nextDate")] = dateStr(r.nextDate);
    o[k("active")] = r.active;
    o[k("createdAt")] = r.createdAt;
    o[k("updatedAt")] = r.updatedAt;
    return o;
}

bool to(const QJsonObject &o, Recurring *r)
{
    r->id = clip(o.value(k("id")), 100);
    r->startDate = dateOf(o.value(k("startDate")));
    const QJsonValue amt = o.value(k("amount"));
    if (r->id.isEmpty() || !r->startDate.isValid() || !amt.isDouble()) return false;
    r->amount = cents(amt);
    r->type = typeFromString(o.value(k("type")).toString());
    r->categoryId = clip(o.value(k("categoryId")), 100);
    r->description = clip(o.value(k("description")), 200);
    r->vendor = clip(o.value(k("vendor")), 120);
    r->paymentMethod = clip(o.value(k("paymentMethod")), 60);
    r->notes = clip(o.value(k("notes")), 2000);
    r->frequency = frequencyFromString(o.value(k("frequency")).toString());
    r->day = std::clamp(o.value(k("day")).toInt(r->startDate.day()), 1, 31);
    r->endDate = dateOf(o.value(k("endDate")));
    r->nextDate = dateOf(o.value(k("nextDate")));
    if (!r->nextDate.isValid()) r->nextDate = r->startDate;
    r->active = o.value(k("active")).toBool(true);
    r->createdAt = clip(o.value(k("createdAt")), 40);
    r->updatedAt = clip(o.value(k("updatedAt")), 40);
    return true;
}

QJsonObject from(const Account &a)
{
    QJsonObject o;
    o[k("id")] = a.id;
    o[k("name")] = a.name;
    o[k("kind")] = a.kind;
    o[k("flipSigns")] = a.flipSigns;
    o[k("csvMapping")] = a.csvMapping;
    o[k("createdAt")] = a.createdAt;
    o[k("updatedAt")] = a.updatedAt;
    return o;
}

bool to(const QJsonObject &o, Account *a)
{
    a->id = clip(o.value(k("id")), 100);
    a->name = clip(o.value(k("name")), 80).simplified();
    if (a->id.isEmpty() || a->name.isEmpty()) return false;
    a->kind = o.value(k("kind")).toString() == QLatin1String("card") ? QStringLiteral("card") : QStringLiteral("bank");
    a->flipSigns = o.value(k("flipSigns")).toBool(false);
    a->csvMapping = clip(o.value(k("csvMapping")), 2000);
    a->createdAt = clip(o.value(k("createdAt")), 40);
    a->updatedAt = clip(o.value(k("updatedAt")), 40);
    return true;
}

QJsonObject from(const ImportRule &r)
{
    QJsonObject o;
    o[k("id")] = r.id;
    o[k("pattern")] = r.pattern;
    o[k("categoryId")] = r.categoryId;
    o[k("ignore")] = r.ignore;
    o[k("createdAt")] = r.createdAt;
    o[k("updatedAt")] = r.updatedAt;
    return o;
}

bool to(const QJsonObject &o, ImportRule *r)
{
    r->id = clip(o.value(k("id")), 100);
    r->pattern = clip(o.value(k("pattern")), 120).simplified();
    if (r->id.isEmpty() || r->pattern.isEmpty()) return false;
    r->categoryId = clip(o.value(k("categoryId")), 100);
    r->ignore = o.value(k("ignore")).toBool(false);
    r->createdAt = clip(o.value(k("createdAt")), 40);
    r->updatedAt = clip(o.value(k("updatedAt")), 40);
    return true;
}

QJsonObject receiptMeta(const Receipt &r)
{
    QJsonObject o;
    o[k("id")] = r.id;
    o[k("transactionId")] = r.transactionId;
    o[k("name")] = r.name;
    o[k("mime")] = r.mime;
    o[k("size")] = static_cast<double>(r.data.isEmpty() ? r.size : r.data.size());
    o[k("createdAt")] = r.createdAt;
    return o;
}

bool receiptFromMeta(const QJsonObject &o, Receipt *r)
{
    r->id = clip(o.value(k("id")), 100);
    r->transactionId = clip(o.value(k("transactionId")), 100);
    if (r->id.isEmpty() || r->transactionId.isEmpty()) return false;
    r->name = clip(o.value(k("name")), 200);
    if (r->name.isEmpty()) r->name = QStringLiteral("receipt");
    r->mime = clip(o.value(k("mime")), 100);
    if (r->mime.isEmpty()) r->mime = QStringLiteral("application/octet-stream");
    r->size = static_cast<qint64>(o.value(k("size")).toDouble());
    r->createdAt = clip(o.value(k("createdAt")), 40);
    if (r->createdAt.isEmpty()) r->createdAt = util::nowIso();
    return true;
}

} // namespace json
