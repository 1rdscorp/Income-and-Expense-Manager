#include "Defaults.h"

/* Note: this file contains UTF-8 emoji. MSVC is built with /utf-8 (see CMakeLists.txt). */

namespace defaults {

static QString u(const char *s) { return QString::fromUtf8(s); }

const QVector<DefaultCategory> &categories()
{
    static const QVector<DefaultCategory> list = {
        // Income
        {u("Sales Revenue"), TxType::Income, u("🛒")},
        {u("Service Income"), TxType::Income, u("🧰")},
        {u("Consulting Fees"), TxType::Income, u("💼")},
        {u("Interest Income"), TxType::Income, u("🏦")},
        {u("Refunds & Rebates"), TxType::Income, u("↩")},
        {u("Grants & Subsidies"), TxType::Income, u("🎗")},
        {u("Other Income"), TxType::Income, u("➕")},
        // Expenses
        {u("Advertising & Marketing"), TxType::Expense, u("📣")},
        {u("Bank & Merchant Fees"), TxType::Expense, u("💳")},
        {u("Cost of Goods / Inventory"), TxType::Expense, u("📦")},
        {u("Contract Labor"), TxType::Expense, u("🤝")},
        {u("Equipment & Tools"), TxType::Expense, u("🔨")},
        {u("Insurance"), TxType::Expense, u("🛡")},
        {u("Legal & Professional Fees"), TxType::Expense, u("⚖")},
        {u("Meals & Entertainment"), TxType::Expense, u("🍽")},
        {u("Office Supplies"), TxType::Expense, u("📎")},
        {u("Payroll & Wages"), TxType::Expense, u("👥")},
        {u("Rent & Lease"), TxType::Expense, u("🏢")},
        {u("Repairs & Maintenance"), TxType::Expense, u("🔧")},
        {u("Shipping & Postage"), TxType::Expense, u("🚚")},
        {u("Software & Subscriptions"), TxType::Expense, u("💻")},
        {u("Taxes & Licenses"), TxType::Expense, u("🧾")},
        {u("Telephone & Internet"), TxType::Expense, u("📞")},
        {u("Training & Education"), TxType::Expense, u("🎓")},
        {u("Travel"), TxType::Expense, u("✈")},
        {u("Utilities"), TxType::Expense, u("💡")},
        {u("Vehicle & Fuel"), TxType::Expense, u("⛽")},
        {u("Miscellaneous"), TxType::Expense, u("🗂")},
    };
    return list;
}

const QVector<CurrencyInfo> &currencies()
{
    static const QVector<CurrencyInfo> list = {
        {u("USD"), u("US Dollar"), u("$"), 2},
        {u("EUR"), u("Euro"), u("€"), 2},
        {u("GBP"), u("British Pound"), u("£"), 2},
        {u("CAD"), u("Canadian Dollar"), u("CA$"), 2},
        {u("AUD"), u("Australian Dollar"), u("A$"), 2},
        {u("NZD"), u("New Zealand Dollar"), u("NZ$"), 2},
        {u("INR"), u("Indian Rupee"), u("₹"), 2},
        {u("LKR"), u("Sri Lankan Rupee"), u("Rs "), 2},
        {u("PKR"), u("Pakistani Rupee"), u("Rs "), 2},
        {u("BDT"), u("Bangladeshi Taka"), u("৳"), 2},
        {u("JPY"), u("Japanese Yen"), u("¥"), 0},
        {u("CNY"), u("Chinese Yuan"), u("CN¥"), 2},
        {u("SGD"), u("Singapore Dollar"), u("S$"), 2},
        {u("MYR"), u("Malaysian Ringgit"), u("RM "), 2},
        {u("PHP"), u("Philippine Peso"), u("₱"), 2},
        {u("AED"), u("UAE Dirham"), u("AED "), 2},
        {u("SAR"), u("Saudi Riyal"), u("SAR "), 2},
        {u("ZAR"), u("South African Rand"), u("R "), 2},
        {u("NGN"), u("Nigerian Naira"), u("₦"), 2},
        {u("KES"), u("Kenyan Shilling"), u("KSh "), 2},
        {u("MXN"), u("Mexican Peso"), u("MX$"), 2},
        {u("BRL"), u("Brazilian Real"), u("R$"), 2},
        {u("CHF"), u("Swiss Franc"), u("CHF "), 2},
        {u("SEK"), u("Swedish Krona"), u("kr "), 2},
    };
    return list;
}

const QStringList &paymentMethods()
{
    static const QStringList list = {u("Cash"), u("Debit card"), u("Credit card"), u("Bank transfer"), u("Check"), u("Mobile payment"), u("Other")};
    return list;
}

const QStringList &iconChoices()
{
    static const QStringList list = {
        u("🛒"), u("🧰"), u("💼"), u("🏦"), u("↩"), u("🎗"), u("➕"), u("📣"), u("💳"), u("📦"),
        u("🤝"), u("🔨"), u("🛡"), u("⚖"), u("🍽"), u("📎"), u("👥"), u("🏢"), u("🔧"), u("🚚"),
        u("💻"), u("🧾"), u("📞"), u("🎓"), u("✈"), u("💡"), u("⛽"), u("🗂"), u("🏷"), u("🎁"),
        u("🧹"), u("🖨"), u("📚"), u("🏥"), u("🅿"), u("🌐"), u("🎨"), u("📷"), u("🧪"), u("🌱"),
    };
    return list;
}

} // namespace defaults
