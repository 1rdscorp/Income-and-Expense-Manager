#pragma once

#include <QString>
#include <QVector>

/* Presets for common U.S. banks and card issuers: default account name and type,
 * statement quirks and where to download statements. */
namespace institutions {

struct Institution {
    QString key;
    QString label;        // shown in the preset list
    QString accountName;  // suggested account name
    QString kind;         // "bank" or "card"
    bool csvChargesPositive = false; // CSV lists charges as positive numbers
    QString downloadSteps;
};

const QVector<Institution> &all();
const Institution *find(const QString &key);

} // namespace institutions
