#include "Institutions.h"

namespace institutions {

namespace {
const QString kUsBank = QStringLiteral(
    "Sign in at usbank.com, open the account and look for “Download transactions” (or Export). Choose the date range and "
    "“Quicken (QFX)”, “QuickBooks (QBO)” or “CSV”.");
const QString kWells = QStringLiteral(
    "Sign in at wellsfargo.com, open the account activity and look for “Download Account Activity”. Choose the account, the dates and "
    "“Quicken (QFX)”, “QuickBooks (QBO)” or “Comma Delimited (CSV)”. Wells Fargo CSV files have no header row — Ledgerly recognises them.");
const QString kAmex = QStringLiteral(
    "Sign in at americanexpress.com, open “Statements & Activity” and look for the Download option. Choose the dates and "
    "“CSV”, “Excel”, “Quicken (QFX)” or “QuickBooks (QBO)”. Amex CSV files list charges as positive amounts — Ledgerly swaps them "
    "automatically for this card.");
} // namespace

const QVector<Institution> &all()
{
    static const QVector<Institution> list = {
        {QStringLiteral("usbank-bank"), QStringLiteral("U.S. Bank — checking / savings"), QStringLiteral("U.S. Bank Checking"), QStringLiteral("bank"), false, kUsBank},
        {QStringLiteral("usbank-card"), QStringLiteral("U.S. Bank — credit card"), QStringLiteral("U.S. Bank Credit Card"), QStringLiteral("card"), false, kUsBank},
        {QStringLiteral("wells-bank"), QStringLiteral("Wells Fargo — checking / savings"), QStringLiteral("Wells Fargo Checking"), QStringLiteral("bank"), false, kWells},
        {QStringLiteral("wells-card"), QStringLiteral("Wells Fargo — credit card"), QStringLiteral("Wells Fargo Credit Card"), QStringLiteral("card"), false, kWells},
        {QStringLiteral("amex-card"), QStringLiteral("American Express — credit / charge card"), QStringLiteral("American Express"), QStringLiteral("card"), true, kAmex},
    };
    return list;
}

const Institution *find(const QString &key)
{
    for (const auto &i : all())
        if (i.key == key) return &i;
    return nullptr;
}

} // namespace institutions
