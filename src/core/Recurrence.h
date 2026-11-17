#pragma once

#include "Models.h"

/* Date arithmetic for recurring transactions. */
namespace recurrence {

// The occurrence after `d` for a rule with this frequency and preferred day of month.
// Monthly dates are clamped to short months (31st -> 28/29 Feb -> 31 Mar).
QDate advance(const QDate &d, Frequency f, int day);

// First occurrence of the rule on or after `from` (never before its start date).
QDate firstOnOrAfter(const Recurring &r, const QDate &from);

// Occurrences of the rule in [from, to], starting at its next date. At most `limit`.
QVector<QDate> occurrences(const Recurring &r, const QDate &from, const QDate &to, int limit = 400);

// "Monthly on the 1st", "Weekly on Monday", "Yearly on 5 Mar"...
QString describe(const Recurring &r);

// Average amount per month (weekly × 52/12, quarterly ÷ 3, yearly ÷ 12).
qint64 monthlyEquivalent(const Recurring &r);

QString ordinal(int n);

} // namespace recurrence
