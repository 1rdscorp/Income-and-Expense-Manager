#include "Util.h"
#include "Defaults.h"

#include <QDateTime>
#include <QLocale>
#include <QRegularExpression>
#include <QUuid>
#include <cmath>

namespace util {

namespace {
QString g_code = QStringLiteral("USD");
QString g_symbol = QStringLiteral("$");
int g_decimals = 2;
const QChar kMinus(0x2212);
}

QString newId() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }

QString stableId(const QString &key)
{
    static const QUuid ns(QStringLiteral("{6f1c4f8e-2d3a-4b7c-9a51-0e7f2c1d8b44}"));
    return QUuid::createUuidV5(ns, key).toString(QUuid::WithoutBraces);
}

QString nowIso() { return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs); }

void setCurrency(const QString &code)
{
    for (const auto &c : defaults::currencies()) {
        if (c.code == code) {
            g_code = c.code;
            g_symbol = c.symbol;
            g_decimals = c.decimals;
            return;
        }
    }
    g_code = code;
    g_symbol = code + QLatin1Char(' ');
    g_decimals = 2;
}

QString currencyCode() { return g_code; }
QString currencySymbol() { return g_symbol; }
int currencyDecimals() { return g_decimals; }

QString money(qint64 cents)
{
    QLocale loc;
    const double v = static_cast<double>(cents) / 100.0;
    QString num = loc.toString(std::fabs(v), 'f', g_decimals);
    return (cents < 0 ? QString(kMinus) : QString()) + g_symbol + num;
}

QString signedMoney(qint64 cents) { return money(cents); }

QString moneyCompact(qint64 cents)
{
    QLocale loc;
    const double v = std::fabs(static_cast<double>(cents) / 100.0);
    QString s;
    auto trim = [&](double x) {
        QString t = loc.toString(x, 'f', 1);
        const QString zero = QString(loc.decimalPoint()) + QLatin1Char('0');
        if (t.endsWith(zero)) t.chop(zero.size());
        return t;
    };
    if (v >= 1e9) s = trim(v / 1e9) + QLatin1Char('B');
    else if (v >= 1e6) s = trim(v / 1e6) + QLatin1Char('M');
    else if (v >= 1e3) s = trim(v / 1e3) + QLatin1Char('K');
    else s = loc.toString(v, 'f', 0);
    return (cents < 0 ? QString(kMinus) : QString()) + g_symbol + s;
}

QString amountPlain(qint64 cents)
{
    const qint64 a = cents < 0 ? -cents : cents;
    return QStringLiteral("%1%2.%3").arg(cents < 0 ? QStringLiteral("-") : QString()).arg(a / 100).arg(a % 100, 2, 10, QLatin1Char('0'));
}

qint64 parseAmount(const QString &text)
{
    QString s;
    for (const QChar c : text.trimmed()) {
        if ((c >= QLatin1Char('0') && c <= QLatin1Char('9')) || c == QLatin1Char('.') || c == QLatin1Char(',') || c == QLatin1Char('-'))
            s += c;
    }
    if (s.isEmpty()) return -1;
    const int lastDot = s.lastIndexOf(QLatin1Char('.'));
    const int lastComma = s.lastIndexOf(QLatin1Char(','));
    if (lastComma > lastDot) {
        static const QRegularExpression decimalComma(QStringLiteral(",\\d{1,2}$"));
        if (decimalComma.match(s).hasMatch()) {
            s.remove(QLatin1Char('.'));
            s.replace(QLatin1Char(','), QLatin1Char('.'));
        } else {
            s.remove(QLatin1Char(','));
        }
    } else {
        s.remove(QLatin1Char(','));
    }
    bool ok = false;
    const double v = s.toDouble(&ok);
    if (!ok || !std::isfinite(v) || v < 0 || v > 1e13) return -1;
    return static_cast<qint64>(std::llround(v * 100.0));
}

QString monthName(int month, bool shortName)
{
    return QLocale().standaloneMonthName(month, shortName ? QLocale::ShortFormat : QLocale::LongFormat);
}

QString monthLabel(int year, int month, bool shortName)
{
    return QStringLiteral("%1 %2").arg(monthName(month, shortName)).arg(year);
}

QString fmtDate(const QDate &d) { return QLocale().toString(d, QStringLiteral("d MMM yyyy")); }
QString fmtDateLong(const QDate &d) { return QLocale().toString(d, QStringLiteral("ddd, d MMM yyyy")); }

QString fmtDay(const QDate &d)
{
    const QDate today = QDate::currentDate();
    if (d == today) return QStringLiteral("Today");
    if (d == today.addDays(-1)) return QStringLiteral("Yesterday");
    return fmtDateLong(d);
}

QString fmtDateTime(const QString &iso)
{
    QDateTime dt = QDateTime::fromString(iso, Qt::ISODateWithMs);
    if (!dt.isValid()) dt = QDateTime::fromString(iso, Qt::ISODate);
    if (!dt.isValid()) return iso;
    return QLocale().toString(dt.toLocalTime(), QStringLiteral("d MMM yyyy, HH:mm"));
}

QString fmtBytes(qint64 b)
{
    if (b < 1024) return QStringLiteral("%1 B").arg(b);
    if (b < 1024 * 1024) return QStringLiteral("%1 KB").arg(b / 1024);
    return QStringLiteral("%1 MB").arg(static_cast<double>(b) / (1024.0 * 1024.0), 0, 'f', 1);
}

double pct(qint64 part, qint64 whole)
{
    if (!whole) return 0.0;
    return std::round(static_cast<double>(part) / static_cast<double>(whole) * 1000.0) / 10.0;
}

QString pctText(qint64 part, qint64 whole)
{
    return QLocale().toString(pct(part, whole), 'f', 1) + QLatin1Char('%');
}

QString toCsv(const QVector<QStringList> &rows)
{
    QString out = QString(QChar(0xFEFF)); // BOM so Excel detects UTF-8
    for (const QStringList &r : rows) {
        QStringList cells;
        for (QString c : r) {
            if (c.contains(QLatin1Char('"')) || c.contains(QLatin1Char(',')) || c.contains(QLatin1Char('\n')) || c.contains(QLatin1Char('\r'))) {
                c.replace(QLatin1Char('"'), QStringLiteral("\"\""));
                c = QLatin1Char('"') + c + QLatin1Char('"');
            }
            cells << c;
        }
        out += cells.join(QLatin1Char(',')) + QStringLiteral("\r\n");
    }
    return out;
}

QString slug(const QString &s)
{
    QString out;
    bool dash = false;
    for (const QChar c : s.toLower()) {
        if ((c >= QLatin1Char('a') && c <= QLatin1Char('z')) || (c >= QLatin1Char('0') && c <= QLatin1Char('9'))) {
            out += c;
            dash = false;
        } else if (!dash && !out.isEmpty()) {
            out += QLatin1Char('-');
            dash = true;
        }
    }
    while (out.endsWith(QLatin1Char('-'))) out.chop(1);
    return out.isEmpty() ? QStringLiteral("business") : out;
}

} // namespace util
