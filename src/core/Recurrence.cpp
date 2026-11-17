#include "Recurrence.h"

#include <QLocale>
#include <algorithm>
#include <cmath>

namespace recurrence {

namespace {
QDate addMonthsClamped(const QDate &d, int months, int day)
{
    const QDate first = QDate(d.year(), d.month(), 1).addMonths(months);
    return QDate(first.year(), first.month(), std::clamp(day, 1, first.daysInMonth()));
}
} // namespace

QDate advance(const QDate &d, Frequency f, int day)
{
    if (!d.isValid()) return {};
    switch (f) {
    case Frequency::Weekly: return d.addDays(7);
    case Frequency::Quarterly: return addMonthsClamped(d, 3, day);
    case Frequency::Yearly: return addMonthsClamped(d, 12, day);
    default: return addMonthsClamped(d, 1, day);
    }
}

QDate firstOnOrAfter(const Recurring &r, const QDate &from)
{
    QDate d = r.startDate;
    if (!d.isValid()) return {};
    int guard = 0;
    while (d < from && guard++ < 5000) d = advance(d, r.frequency, r.day);
    return d;
}

QVector<QDate> occurrences(const Recurring &r, const QDate &from, const QDate &to, int limit)
{
    QVector<QDate> out;
    QDate d = r.nextDate.isValid() ? r.nextDate : r.startDate;
    int guard = 0;
    while (d.isValid() && d <= to && out.size() < limit && guard++ < 5000) {
        if (r.endDate.isValid() && d > r.endDate) break;
        if (d >= from) out << d;
        d = advance(d, r.frequency, r.day);
    }
    return out;
}

QString ordinal(int n)
{
    const int mod100 = n % 100;
    const char *suffix = "th";
    if (mod100 < 11 || mod100 > 13) {
        if (n % 10 == 1) suffix = "st";
        else if (n % 10 == 2) suffix = "nd";
        else if (n % 10 == 3) suffix = "rd";
    }
    return QString::number(n) + QLatin1String(suffix);
}

QString describe(const Recurring &r)
{
    QLocale loc;
    switch (r.frequency) {
    case Frequency::Weekly:
        return QStringLiteral("Weekly on %1").arg(loc.dayName(r.startDate.isValid() ? r.startDate.dayOfWeek() : 1));
    case Frequency::Quarterly:
        return QStringLiteral("Every 3 months on the %1").arg(ordinal(r.day));
    case Frequency::Yearly: {
        const int month = r.startDate.isValid() ? r.startDate.month() : 1;
        return QStringLiteral("Yearly on %1 %2").arg(r.day).arg(loc.monthName(month, QLocale::ShortFormat));
    }
    default:
        return QStringLiteral("Monthly on the %1").arg(ordinal(r.day));
    }
}

qint64 monthlyEquivalent(const Recurring &r)
{
    switch (r.frequency) {
    case Frequency::Weekly: return static_cast<qint64>(std::llround(static_cast<double>(r.amount) * 52.0 / 12.0));
    case Frequency::Quarterly: return static_cast<qint64>(std::llround(static_cast<double>(r.amount) / 3.0));
    case Frequency::Yearly: return static_cast<qint64>(std::llround(static_cast<double>(r.amount) / 12.0));
    default: return r.amount;
    }
}

} // namespace recurrence
