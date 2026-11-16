#pragma once

#include "Models.h"
#include <QStringList>

namespace defaults {

struct DefaultCategory {
    QString name;
    TxType type;
    QString icon;
};

struct CurrencyInfo {
    QString code;
    QString name;
    QString symbol;
    int decimals;
};

const QVector<DefaultCategory> &categories();
const QVector<CurrencyInfo> &currencies();
const QStringList &paymentMethods();
const QStringList &iconChoices();

} // namespace defaults
