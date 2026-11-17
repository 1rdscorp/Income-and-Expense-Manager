#pragma once

#include "Models.h"
#include <QJsonObject>

/* JSON (de)serialisation of each record type. Shared by backups and the
 * shared-folder sync journal so both always agree on the format. */
namespace json {

QString clip(const QJsonValue &v, int max);

QJsonObject from(const Category &c);
QJsonObject from(const Transaction &t);
QJsonObject from(const Recurring &r);
QJsonObject from(const Account &a);
QJsonObject from(const ImportRule &r);
QJsonObject receiptMeta(const Receipt &r); // without the file data

// Each returns false when required fields are missing/invalid.
bool to(const QJsonObject &o, Category *c);
bool to(const QJsonObject &o, Transaction *t);
bool to(const QJsonObject &o, Recurring *r);
bool to(const QJsonObject &o, Account *a);
bool to(const QJsonObject &o, ImportRule *r);
bool receiptFromMeta(const QJsonObject &o, Receipt *r);

} // namespace json
