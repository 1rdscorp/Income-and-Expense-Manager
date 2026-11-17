#include "BankImport.h"
#include "Store.h"
#include "Util.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QRegularExpression>
#include <algorithm>
#include <cmath>

namespace bank {

namespace {

QString decodeText(const QByteArray &data)
{
    QByteArray d = data;
    if (d.startsWith("\xEF\xBB\xBF")) d.remove(0, 3);
    const QString utf8 = QString::fromUtf8(d);
    if (!utf8.contains(QChar(QChar::ReplacementCharacter))) return utf8;
    return QString::fromLatin1(d); // older Windows-1252 / Latin-1 exports
}

QString fingerprint(const StatementLine &l, int occurrence)
{
    const QString key = QStringLiteral("%1|%2|%3|%4").arg(l.date.toString(Qt::ISODate)).arg(l.amount).arg(l.description.simplified().toLower()).arg(occurrence);
    return QStringLiteral("fp:") + QString::fromLatin1(QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha1).toHex().left(20));
}

// Gives lines without a bank id a stable fingerprint (identical lines numbered in order).
void assignFingerprints(QVector<StatementLine> &lines)
{
    QHash<QString, int> seen;
    for (auto &l : lines) {
        if (!l.externalId.isEmpty()) continue;
        const QString base = QStringLiteral("%1|%2|%3").arg(l.date.toString(Qt::ISODate)).arg(l.amount).arg(l.description.simplified().toLower());
        const int n = seen.value(base, 0);
        seen.insert(base, n + 1);
        l.externalId = fingerprint(l, n);
    }
}

QString ofxTag(const QString &block, const QString &tag)
{
    const QRegularExpression re(QStringLiteral("<%1>\\s*([^<\\r\\n]*)").arg(tag), QRegularExpression::CaseInsensitiveOption);
    const auto m = re.match(block);
    return m.hasMatch() ? m.captured(1).trimmed() : QString();
}

QString unescapeSgml(QString s)
{
    s.replace(QStringLiteral("&amp;"), QStringLiteral("&"));
    s.replace(QStringLiteral("&lt;"), QStringLiteral("<"));
    s.replace(QStringLiteral("&gt;"), QStringLiteral(">"));
    s.replace(QStringLiteral("&apos;"), QStringLiteral("'"));
    s.replace(QStringLiteral("&quot;"), QStringLiteral("\""));
    return s;
}

QString normHeader(const QString &h) { return h.toLower().remove(QRegularExpression(QStringLiteral("[^a-z]"))); }

bool headerIs(const QString &h, std::initializer_list<const char *> names)
{
    const QString n = normHeader(h);
    for (const char *x : names)
        if (n == QLatin1String(x)) return true;
    return false;
}

bool headerHas(const QString &h, std::initializer_list<const char *> parts)
{
    const QString n = normHeader(h);
    for (const char *x : parts)
        if (n.contains(QLatin1String(x))) return true;
    return false;
}

QDate parseDate(const QString &s, const QString &format)
{
    const QString t = s.trimmed();
    QDate d = QDate::fromString(t, format);
    if (!d.isValid()) d = QLocale::c().toDate(t, format);
    if (d.isValid() && format.contains(QLatin1String("yy")) && !format.contains(QLatin1String("yyyy")) && d.year() < 1970) d = d.addYears(100);
    return d;
}

struct Keyword {
    const char *word;
    const char *category;
};
// Built-in hints (matched against default category names when no rule or history applies)
const Keyword kKeywords[] = {
    {"fuel", "Vehicle & Fuel"},     {"petrol", "Vehicle & Fuel"},   {"gas station", "Vehicle & Fuel"}, {"shell", "Vehicle & Fuel"},
    {"chevron", "Vehicle & Fuel"},  {"exxon", "Vehicle & Fuel"},    {"bp ", "Vehicle & Fuel"},       {"parking", "Vehicle & Fuel"},
    {"uber", "Travel"},             {"lyft", "Travel"},             {"airline", "Travel"},           {"airways", "Travel"},
    {"hotel", "Travel"},            {"airbnb", "Travel"},           {"booking.com", "Travel"},       {"expedia", "Travel"},
    {"restaurant", "Meals & Entertainment"}, {"cafe", "Meals & Entertainment"}, {"coffee", "Meals & Entertainment"},
    {"starbucks", "Meals & Entertainment"},  {"mcdonald", "Meals & Entertainment"}, {"doordash", "Meals & Entertainment"},
    {"ubereats", "Meals & Entertainment"},   {"grubhub", "Meals & Entertainment"},
    {"google", "Software & Subscriptions"},  {"microsoft", "Software & Subscriptions"}, {"adobe", "Software & Subscriptions"},
    {"dropbox", "Software & Subscriptions"}, {"zoom", "Software & Subscriptions"},      {"slack", "Software & Subscriptions"},
    {"intuit", "Software & Subscriptions"},  {"xero", "Software & Subscriptions"},      {"github", "Software & Subscriptions"},
    {"netflix", "Software & Subscriptions"}, {"spotify", "Software & Subscriptions"},   {"apple.com", "Software & Subscriptions"},
    {"shopify", "Software & Subscriptions"}, {"canva", "Software & Subscriptions"},
    {"facebook", "Advertising & Marketing"}, {"meta ads", "Advertising & Marketing"},   {"google ads", "Advertising & Marketing"},
    {"mailchimp", "Advertising & Marketing"},
    {"insurance", "Insurance"},     {"rent", "Rent & Lease"},       {"lease", "Rent & Lease"},
    {"electric", "Utilities"},      {"water", "Utilities"},         {"power", "Utilities"},          {"energy", "Utilities"},
    {"internet", "Telephone & Internet"}, {"telecom", "Telephone & Internet"}, {"mobile", "Telephone & Internet"},
    {"verizon", "Telephone & Internet"},  {"at&t", "Telephone & Internet"},    {"vodafone", "Telephone & Internet"},
    {"comcast", "Telephone & Internet"},  {"broadband", "Telephone & Internet"},
    {"fedex", "Shipping & Postage"}, {"ups ", "Shipping & Postage"}, {"usps", "Shipping & Postage"}, {"dhl", "Shipping & Postage"},
    {"post office", "Shipping & Postage"}, {"courier", "Shipping & Postage"},
    {"staples", "Office Supplies"}, {"office depot", "Office Supplies"}, {"officeworks", "Office Supplies"},
    {"fee", "Bank & Merchant Fees"}, {"charge", "Bank & Merchant Fees"}, {"stripe", "Bank & Merchant Fees"},
    {"square", "Bank & Merchant Fees"}, {"paypal fee", "Bank & Merchant Fees"},
    {"payroll", "Payroll & Wages"}, {"salary", "Payroll & Wages"}, {"wages", "Payroll & Wages"},
    {"tax", "Taxes & Licenses"},    {"irs", "Taxes & Licenses"},    {"hmrc", "Taxes & Licenses"},    {"licen", "Taxes & Licenses"},
    {"attorney", "Legal & Professional Fees"}, {"legal", "Legal & Professional Fees"}, {"accountant", "Legal & Professional Fees"},
    {"cpa", "Legal & Professional Fees"},
    {"repair", "Repairs & Maintenance"}, {"maintenance", "Repairs & Maintenance"},
    {"course", "Training & Education"},  {"udemy", "Training & Education"}, {"training", "Training & Education"},
    {"interest", "Interest Income"}, {"refund", "Refunds & Rebates"}, {"rebate", "Refunds & Rebates"}, {"cashback", "Refunds & Rebates"},
    {"grant", "Grants & Subsidies"},
};

const Category *categoryByName(const Store &store, const QString &name, TxType type)
{
    for (const auto &c : store.categories())
        if (c.type == type && c.name.compare(name, Qt::CaseInsensitive) == 0) return &c;
    return nullptr;
}

} // namespace

/* ---------------- CSV mapping ---------------- */

QString CsvMapping::toJson() const
{
    QJsonObject o;
    o[QStringLiteral("delimiter")] = QString(delimiter);
    o[QStringLiteral("hasHeader")] = hasHeader;
    o[QStringLiteral("date")] = date;
    o[QStringLiteral("description")] = description;
    o[QStringLiteral("amount")] = amount;
    o[QStringLiteral("debit")] = debit;
    o[QStringLiteral("credit")] = credit;
    o[QStringLiteral("memo")] = memo;
    o[QStringLiteral("dateFormat")] = dateFormat;
    return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
}

CsvMapping CsvMapping::fromJson(const QString &json)
{
    CsvMapping m;
    const QJsonObject o = QJsonDocument::fromJson(json.toUtf8()).object();
    if (o.isEmpty()) return m;
    const QString d = o.value(QStringLiteral("delimiter")).toString();
    if (!d.isEmpty()) m.delimiter = d.at(0);
    m.hasHeader = o.value(QStringLiteral("hasHeader")).toBool(true);
    m.date = o.value(QStringLiteral("date")).toInt(-1);
    m.description = o.value(QStringLiteral("description")).toInt(-1);
    m.amount = o.value(QStringLiteral("amount")).toInt(-1);
    m.debit = o.value(QStringLiteral("debit")).toInt(-1);
    m.credit = o.value(QStringLiteral("credit")).toInt(-1);
    m.memo = o.value(QStringLiteral("memo")).toInt(-1);
    m.dateFormat = o.value(QStringLiteral("dateFormat")).toString();
    return m;
}

/* ---------------- Parsing ---------------- */

QStringList dateFormats()
{
    return {QStringLiteral("yyyy-MM-dd"), QStringLiteral("dd/MM/yyyy"), QStringLiteral("MM/dd/yyyy"), QStringLiteral("d/M/yyyy"),
            QStringLiteral("M/d/yyyy"),   QStringLiteral("dd.MM.yyyy"), QStringLiteral("d.M.yyyy"),   QStringLiteral("dd-MM-yyyy"),
            QStringLiteral("MM-dd-yyyy"), QStringLiteral("yyyy/MM/dd"), QStringLiteral("dd/MM/yy"),   QStringLiteral("MM/dd/yy"),
            QStringLiteral("d/M/yy"),     QStringLiteral("M/d/yy"),     QStringLiteral("d MMM yyyy"), QStringLiteral("dd MMM yyyy"),
            QStringLiteral("dd-MMM-yyyy"), QStringLiteral("dd-MMM-yy"), QStringLiteral("d-MMM-yy"),  QStringLiteral("MMM d, yyyy"),
            QStringLiteral("MMM dd, yyyy"), QStringLiteral("yyyyMMdd"), QStringLiteral("yyyy-MM-ddTHH:mm:ss"), QStringLiteral("yyyy-MM-dd HH:mm:ss")};
}

QString detectDateFormat(const QStringList &samples, int preferMonthFirst)
{
    QStringList values;
    for (const auto &s : samples)
        if (!s.trimmed().isEmpty()) values << s.trimmed();
    if (values.isEmpty()) return {};
    // Prefer day-first when the system's short date format is day-first (ambiguous files like 03/04/2026).
    const bool dayFirst = preferMonthFirst == 0 ? QLocale().dateFormat(QLocale::ShortFormat).trimmed().startsWith(QLatin1Char('d'))
                                                : preferMonthFirst < 0;
    QStringList formats = dateFormats();
    if (!dayFirst) {
        // move month-first variants ahead of their day-first twins
        std::stable_sort(formats.begin(), formats.end(), [](const QString &a, const QString &b) {
            return a.startsWith(QLatin1Char('M')) && !b.startsWith(QLatin1Char('M')) && a.size() == b.size();
        });
    }
    QString best;
    int bestCount = 0;
    for (const QString &f : formats) {
        int ok = 0;
        for (const auto &v : values)
            if (parseDate(v, f).isValid()) ++ok;
        if (ok > bestCount) {
            best = f;
            bestCount = ok;
        }
        if (ok == values.size()) return f;
    }
    return bestCount * 10 >= values.size() * 8 ? best : QString();
}

qint64 parseSignedAmount(const QString &text, bool *ok)
{
    QString s = text.trimmed();
    bool negative = false;
    if (s.isEmpty()) {
        *ok = false;
        return 0;
    }
    if ((s.startsWith(QLatin1Char('(')) && s.endsWith(QLatin1Char(')')))) {
        negative = true;
        s = s.mid(1, s.size() - 2);
    }
    const QString up = s.toUpper();
    if (up.endsWith(QLatin1String("DR")) || up.endsWith(QLatin1String(" D"))) negative = true;
    if (s.contains(QLatin1Char('-')) || s.contains(QChar(0x2212))) negative = true;
    s.remove(QLatin1Char('-'));
    s.remove(QChar(0x2212));
    const qint64 v = util::parseAmount(s);
    *ok = v >= 0;
    return negative ? -v : v;
}

QList<QStringList> readCsv(const QByteArray &data, QChar *delimiter)
{
    const QString text = decodeText(data);
    // pick the delimiter that splits the first lines most consistently
    QChar delim = QLatin1Char(',');
    if (delimiter && !delimiter->isNull()) delim = *delimiter;
    else {
        const QStringList sample = text.left(4000).split(QLatin1Char('\n'));
        int bestScore = -1;
        for (QChar d : {QLatin1Char(','), QLatin1Char(';'), QLatin1Char('\t'), QLatin1Char('|')}) {
            QVector<int> counts;
            for (const QString &l : sample)
                if (!l.trimmed().isEmpty()) counts << static_cast<int>(l.count(d));
            if (counts.isEmpty()) continue;
            const int first = counts.first();
            int same = 0;
            for (int c : counts)
                if (c == first) ++same;
            const int score = first > 0 ? same * 10 + first : -1;
            if (score > bestScore) {
                bestScore = score;
                delim = d;
            }
        }
        if (delimiter) *delimiter = delim;
    }
    QList<QStringList> rows;
    QStringList row;
    QString cell;
    bool inQuotes = false;
    for (int i = 0; i < text.size(); ++i) {
        const QChar c = text.at(i);
        if (inQuotes) {
            if (c == QLatin1Char('"')) {
                if (i + 1 < text.size() && text.at(i + 1) == QLatin1Char('"')) {
                    cell += QLatin1Char('"');
                    ++i;
                } else {
                    inQuotes = false;
                }
            } else {
                cell += c;
            }
        } else if (c == QLatin1Char('"')) {
            inQuotes = true;
        } else if (c == delim) {
            row << cell.trimmed();
            cell.clear();
        } else if (c == QLatin1Char('\n') || c == QLatin1Char('\r')) {
            if (c == QLatin1Char('\r') && i + 1 < text.size() && text.at(i + 1) == QLatin1Char('\n')) ++i;
            row << cell.trimmed();
            cell.clear();
            bool blank = true;
            for (const auto &x : row)
                if (!x.isEmpty()) blank = false;
            if (!blank) rows << row;
            row.clear();
        } else {
            cell += c;
        }
    }
    row << cell.trimmed();
    bool blank = true;
    for (const auto &x : row)
        if (!x.isEmpty()) blank = false;
    if (!blank) rows << row;
    return rows;
}

int findHeaderRow(const QList<QStringList> &rows)
{
    for (int r = 0; r < std::min<int>(10, rows.size()); ++r) {
        if (rows[r].size() < 2) continue;
        for (const QString &h : rows[r])
            if (headerHas(h, {"date", "posted"}) && h.size() < 40 && detectDateFormat({h}).isEmpty()) return r;
    }
    return -1;
}

CsvMapping guessCsvMapping(const QList<QStringList> &rows, QChar delimiter)
{
    CsvMapping m;
    m.delimiter = delimiter;
    if (rows.isEmpty()) return m;
    // Some banks put a few info lines before the header
    const int headerRow = findHeaderRow(rows);
    const QStringList header = headerRow >= 0 ? rows[headerRow] : QStringList();
    m.hasHeader = headerRow >= 0;
    for (int c = 0; c < header.size(); ++c) {
        const QString &h = header[c];
        if (m.date < 0 && headerHas(h, {"date", "posted"})) m.date = c;
        else if (m.debit < 0 && headerHas(h, {"debit", "withdrawal", "moneyout", "paidout", "spent", "outflow"})) m.debit = c;
        else if (m.credit < 0 && headerHas(h, {"credit", "deposit", "moneyin", "paidin", "received", "inflow"})) m.credit = c;
        else if (m.amount < 0 && headerHas(h, {"amount", "value", "sum", "betrag", "importe", "montant"})) m.amount = c;
        else if (m.description < 0 && headerHas(h, {"description", "details", "narrative", "payee", "merchant", "name", "particulars", "transaction", "reference"}))
            m.description = c;
        else if (m.memo < 0 && headerHas(h, {"memo", "note", "category"})) m.memo = c;
    }
    if (headerIs(header.value(m.amount), {"balance"})) m.amount = -1;
    if (m.amount >= 0 && (m.debit >= 0 || m.credit >= 0)) {
        // a single signed amount column wins over partial debit/credit detection
        if (m.debit < 0 || m.credit < 0) m.debit = m.credit = -1;
    }

    // Without a header, infer from the content of the first rows
    const int start = headerRow >= 0 ? headerRow + 1 : 0;
    const int cols = rows.value(start).size();
    if (m.date < 0 || m.description < 0 || (m.amount < 0 && m.debit < 0 && m.credit < 0)) {
        QVector<int> dateHits(cols), numHits(cols), textLen(cols);
        for (int r = start; r < std::min<int>(rows.size(), start + 20); ++r)
            for (int c = 0; c < std::min<int>(cols, rows[r].size()); ++c) {
                const QString v = rows[r][c];
                if (!detectDateFormat({v}).isEmpty() && v.size() >= 6) ++dateHits[c];
                bool ok = false;
                parseSignedAmount(v, &ok);
                if (ok && v.contains(QRegularExpression(QStringLiteral("\\d")))) ++numHits[c];
                textLen[c] += v.size();
            }
        if (m.date < 0)
            for (int c = 0; c < cols; ++c)
                if (dateHits[c] > 0 && (m.date < 0 || dateHits[c] > dateHits[m.date])) m.date = c;
        if (m.amount < 0 && m.debit < 0 && m.credit < 0)
            for (int c = 0; c < cols; ++c)
                if (c != m.date && numHits[c] > 0 && (m.amount < 0 || numHits[c] > numHits[m.amount])) m.amount = c;
        if (m.description < 0)
            for (int c = 0; c < cols; ++c)
                if (c != m.date && c != m.amount && (m.description < 0 || textLen[c] > textLen[m.description])) m.description = c;
    }
    if (m.date >= 0) {
        QStringList samples;
        for (int r = start; r < std::min<int>(rows.size(), start + 60); ++r) samples << rows[r].value(m.date);
        m.dateFormat = detectDateFormat(samples);
    }
    return m;
}

ParseResult parseCsv(const QByteArray &data, const CsvMapping *mapping)
{
    ParseResult res;
    res.format = QStringLiteral("CSV");
    QChar delim = mapping && mapping->isValid() ? mapping->delimiter : QChar();
    res.csvRows = readCsv(data, &delim);
    if (res.csvRows.isEmpty()) {
        res.error = QStringLiteral("The file is empty.");
        return res;
    }
    res.csvHeaderRow = findHeaderRow(res.csvRows);
    CsvMapping m = (mapping && mapping->isValid()) ? *mapping : guessCsvMapping(res.csvRows, delim);
    m.delimiter = delim;
    res.csvMapping = m;
    if (!m.isValid()) {
        res.error = QStringLiteral("Couldn't recognise the columns in this CSV file. Choose which column holds the date, description and amount.");
        return res;
    }
    // skip preamble lines before the header
    int start = 0;
    if (m.hasHeader) {
        for (int r = 0; r < std::min<int>(10, res.csvRows.size()); ++r)
            if (!parseDate(res.csvRows[r].value(m.date), m.dateFormat).isValid()) start = r + 1;
            else break;
    }
    for (int r = start; r < res.csvRows.size(); ++r) {
        const QStringList &row = res.csvRows[r];
        const QDate d = parseDate(row.value(m.date), m.dateFormat);
        if (!d.isValid()) continue; // totals / blank / balance lines
        qint64 amount = 0;
        bool ok = false;
        if (m.amount >= 0) {
            amount = parseSignedAmount(row.value(m.amount), &ok);
        } else {
            bool okD = false, okC = false;
            const qint64 debit = std::llabs(parseSignedAmount(row.value(m.debit), &okD));
            const qint64 credit = std::llabs(parseSignedAmount(row.value(m.credit), &okC));
            ok = okD || okC;
            amount = (okC ? credit : 0) - (okD ? debit : 0);
        }
        if (!ok || amount == 0) continue;
        StatementLine l;
        l.date = d;
        l.amount = amount;
        l.description = row.value(m.description).simplified();
        if (m.memo >= 0) l.memo = row.value(m.memo).simplified();
        res.lines << l;
    }
    if (res.lines.isEmpty()) res.error = QStringLiteral("No transactions were found with the selected columns and date format.");
    assignFingerprints(res.lines);
    return res;
}

ParseResult parseOfx(const QByteArray &data)
{
    ParseResult res;
    res.format = QStringLiteral("OFX");
    const QString text = decodeText(data);
    res.looksLikeCard = text.contains(QLatin1String("CREDITCARDMSGSRSV1"), Qt::CaseInsensitive) || text.contains(QLatin1String("<CCSTMTRS>"), Qt::CaseInsensitive);
    res.accountHint = ofxTag(text, QStringLiteral("ACCTID"));
    static const QRegularExpression start(QStringLiteral("<STMTTRN>"), QRegularExpression::CaseInsensitiveOption);
    auto it = start.globalMatch(text);
    QVector<int> starts;
    while (it.hasNext()) starts << static_cast<int>(it.next().capturedEnd());
    for (int i = 0; i < starts.size(); ++i) {
        int end = i + 1 < starts.size() ? starts[i + 1] : static_cast<int>(text.size());
        const int close = text.indexOf(QLatin1String("</STMTTRN>"), starts[i], Qt::CaseInsensitive);
        if (close >= 0 && close < end) end = close;
        const QString block = text.mid(starts[i], end - starts[i]);
        StatementLine l;
        const QString dt = ofxTag(block, QStringLiteral("DTPOSTED"));
        l.date = QDate::fromString(dt.left(8), QStringLiteral("yyyyMMdd"));
        bool ok = false;
        QString amt = ofxTag(block, QStringLiteral("TRNAMT"));
        amt.replace(QLatin1Char(','), QLatin1Char('.'));
        const double v = amt.toDouble(&ok);
        if (!l.date.isValid() || !ok || v == 0.0) continue;
        l.amount = static_cast<qint64>(std::llround(v * 100.0));
        QString name = unescapeSgml(ofxTag(block, QStringLiteral("NAME")));
        if (name.isEmpty()) name = unescapeSgml(ofxTag(block, QStringLiteral("PAYEE")));
        const QString memo = unescapeSgml(ofxTag(block, QStringLiteral("MEMO")));
        l.description = (name.isEmpty() ? memo : name).simplified();
        if (!name.isEmpty() && memo != name) l.memo = memo.simplified();
        const QString fitid = ofxTag(block, QStringLiteral("FITID"));
        if (!fitid.isEmpty()) l.externalId = QStringLiteral("ofx:") + fitid;
        res.lines << l;
    }
    if (res.lines.isEmpty()) res.error = QStringLiteral("No transactions were found in this OFX/QFX file.");
    assignFingerprints(res.lines);
    return res;
}

ParseResult parseQif(const QByteArray &data)
{
    ParseResult res;
    res.format = QStringLiteral("QIF");
    const QString text = decodeText(data);
    res.looksLikeCard = text.contains(QLatin1String("!Type:CCard"), Qt::CaseInsensitive);
    struct Raw {
        QString date, amount, payee, memo, number;
    };
    QVector<Raw> raws;
    Raw cur;
    bool any = false;
    for (QString line : text.split(QLatin1Char('\n'))) {
        line = line.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('!'))) continue;
        const QChar code = line.at(0);
        const QString val = line.mid(1).trimmed();
        if (code == QLatin1Char('^')) {
            if (any) raws << cur;
            cur = Raw();
            any = false;
            continue;
        }
        any = true;
        if (code == QLatin1Char('D')) cur.date = val;
        else if (code == QLatin1Char('T') || code == QLatin1Char('U')) cur.amount = val;
        else if (code == QLatin1Char('P')) cur.payee = val;
        else if (code == QLatin1Char('M')) cur.memo = val;
        else if (code == QLatin1Char('N')) cur.number = val;
    }
    if (any) raws << cur;
    QStringList dates;
    static const QRegularExpression shortYear(QStringLiteral("^(\\d{1,2})[/.-](\\d{1,2})[/.-](\\d{2})$"));
    for (auto &r : raws) {
        r.date.replace(QLatin1Char('\''), QLatin1Char('/')); // 1/31'26
        r.date.replace(QLatin1Char(' '), QString());
        const auto m = shortYear.match(r.date);
        if (m.hasMatch()) r.date = QStringLiteral("%1/%2/20%3").arg(m.captured(1), m.captured(2), m.captured(3));
        dates << r.date;
    }
    const QString fmt = detectDateFormat(dates, 1); // QIF dates are month-first
    for (const auto &r : raws) {
        StatementLine l;
        l.date = parseDate(r.date, fmt);
        bool ok = false;
        l.amount = parseSignedAmount(r.amount, &ok);
        if (!l.date.isValid() || !ok || l.amount == 0) continue;
        l.description = (r.payee.isEmpty() ? r.memo : r.payee).simplified();
        if (!r.payee.isEmpty() && r.memo != r.payee) l.memo = r.memo.simplified();
        res.lines << l;
    }
    if (res.lines.isEmpty()) res.error = QStringLiteral("No transactions were found in this QIF file.");
    assignFingerprints(res.lines);
    return res;
}

ParseResult parseData(const QByteArray &data, const QString &fileName, const CsvMapping *csv)
{
    const QString ext = QFileInfo(fileName).suffix().toLower();
    const QByteArray head = data.left(2000).toUpper();
    if (ext == QLatin1String("ofx") || ext == QLatin1String("qfx") || ext == QLatin1String("qbo") || head.contains("OFXHEADER") || head.contains("<OFX>"))
        return parseOfx(data);
    if (ext == QLatin1String("qif") || head.startsWith("!TYPE")) return parseQif(data);
    return parseCsv(data, csv);
}

ParseResult parseFile(const QString &path, const CsvMapping *csv)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        ParseResult r;
        r.error = QStringLiteral("Can't open %1: %2").arg(QFileInfo(path).fileName(), f.errorString());
        return r;
    }
    if (f.size() > 50LL * 1024 * 1024) {
        ParseResult r;
        r.error = QStringLiteral("The file is too large to be a statement.");
        return r;
    }
    return parseData(f.readAll(), path, csv);
}

/* ---------------- Categorisation ---------------- */

QString cleanDescription(const QString &raw)
{
    QString s = raw.simplified();
    static const QRegularExpression prefixes(
        QStringLiteral("^(POS|PURCHASE|CARD PURCHASE|DEBIT CARD PURCHASE|VISA PURCHASE|VISA DEBIT|DEBIT CARD|CHECKCARD|CHECK CARD|"
                       "CONTACTLESS|DIRECT DEBIT|DD|ACH DEBIT|ACH CREDIT|ACH|ONLINE PAYMENT|PAYMENT TO|EFTPOS|EFT|RECURRING PAYMENT|"
                       "PURCHASE AUTHORIZED ON \\d{1,2}/\\d{1,2})\\b[\\s:-]*"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression leadingNumber(QStringLiteral("^\\d{2,4}\\s+"));
    for (int i = 0; i < 3; ++i) {
        s.remove(prefixes);
        s.remove(leadingNumber); // terminal / store numbers after "POS"
    }
    static const QRegularExpression noise(QStringLiteral(
        "(\\b(X{2,}|\\*{2,})\\d{2,}\\b|\\bCARD\\s*\\d{2,}\\b|\\b\\d{5,}\\b|\\b\\d{1,2}/\\d{1,2}(/\\d{2,4})?\\b|\\b\\d{3}-\\d{3}-\\d{4}\\b|#\\s?\\d+)"),
        QRegularExpression::CaseInsensitiveOption);
    s.remove(noise);
    static const QRegularExpression trailingCountry(QStringLiteral("\\s+[A-Z]{2}$"));
    if (s.size() > 8) s.remove(trailingCountry);
    s = s.simplified();
    s.remove(QRegularExpression(QStringLiteral("^[\\s*:/,.-]+|[\\s*:/,-]+$")));
    if (s.isEmpty()) s = raw.simplified();
    // Title-case shouty bank text
    if (s == s.toUpper()) {
        QString out;
        bool startWord = true;
        for (const QChar c : s.toLower()) {
            out += startWord ? c.toUpper() : c;
            startWord = !c.isLetterOrNumber() && c != QLatin1Char('\'') && c != QLatin1Char('.');
        }
        s = out;
    }
    return s.left(200);
}

QString payeeKey(const QString &description)
{
    QString s = cleanDescription(description).toLower();
    s.remove(QRegularExpression(QStringLiteral("[^a-z ]")));
    const QStringList words = s.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    return words.mid(0, 2).join(QLatin1Char(' '));
}

Suggestion suggestCategory(const Store &store, const QString &description, TxType type)
{
    Suggestion sg;
    const QString text = description.toLower();
    // 1. Your rules (longest pattern first)
    QVector<ImportRule> rules = store.importRules();
    std::sort(rules.begin(), rules.end(), [](const ImportRule &a, const ImportRule &b) { return a.pattern.size() > b.pattern.size(); });
    for (const auto &r : rules) {
        if (!text.contains(r.pattern.toLower())) continue;
        if (r.ignore) {
            sg.ignore = true;
            sg.source = QStringLiteral("rule");
            return sg;
        }
        const Category *c = store.category(r.categoryId);
        if (c && c->type == type) {
            sg.categoryId = c->id;
            sg.source = QStringLiteral("rule");
            return sg;
        }
    }
    // 2. What you chose for this payee before (newest first)
    const QString key = payeeKey(description);
    if (!key.isEmpty()) {
        for (const auto &t : store.transactions()) {
            if (t.type != type) continue;
            if (payeeKey(t.description) == key || (!t.vendor.isEmpty() && payeeKey(t.vendor) == key)) {
                if (store.category(t.categoryId)) {
                    sg.categoryId = t.categoryId;
                    sg.source = QStringLiteral("history");
                    return sg;
                }
            }
        }
    }
    // 3. Built-in keywords
    const QString padded = text + QLatin1Char(' ');
    for (const auto &k : kKeywords) {
        if (!padded.contains(QLatin1String(k.word))) continue;
        if (const Category *c = categoryByName(store, QString::fromUtf8(k.category), type)) {
            sg.categoryId = c->id;
            sg.source = QStringLiteral("keyword");
            return sg;
        }
    }
    // 4. Default
    const char *fallback = type == TxType::Income ? "Sales Revenue" : "Miscellaneous";
    const Category *c = categoryByName(store, QString::fromUtf8(fallback), type);
    if (!c) c = categoryByName(store, type == TxType::Income ? QStringLiteral("Other Income") : QStringLiteral("Other"), type);
    if (!c)
        for (const auto &x : store.categories())
            if (x.type == type) {
                c = &x;
                break;
            }
    if (c) sg.categoryId = c->id;
    sg.source = QStringLiteral("default");
    return sg;
}

QVector<Proposed> prepare(const Store &store, const Account &account, const QVector<StatementLine> &lines)
{
    QVector<Proposed> out;
    QSet<QString> claimed; // existing transactions already matched as possible duplicates
    for (StatementLine l : lines) {
        if (account.flipSigns) l.amount = -l.amount;
        Proposed p;
        p.line = l;
        const TxType type = l.amount >= 0 ? TxType::Income : TxType::Expense;
        Transaction &t = p.tx;
        t.type = type;
        t.amount = std::llabs(l.amount);
        t.date = l.date;
        t.description = cleanDescription(l.description);
        t.notes = l.memo.isEmpty() ? l.description : QStringLiteral("%1 — %2").arg(l.description, l.memo);
        if (t.notes == t.description) t.notes.clear();
        t.paymentMethod = account.name;
        t.accountId = account.id;
        t.externalId = l.externalId;
        const Suggestion sg = suggestCategory(store, l.description + QLatin1Char(' ') + l.memo, type);
        t.categoryId = sg.categoryId;

        if (store.alreadyImported(account.id, l.externalId)) {
            p.status = Proposed::Status::Duplicate;
            p.note = QStringLiteral("Already imported");
            p.include = false;
        } else if (sg.ignore) {
            p.status = Proposed::Status::Ignored;
            p.note = QStringLiteral("Ignored by a rule (e.g. transfer or card payment)");
            p.include = false;
        } else {
            // Same amount and type within 3 days: probably entered by hand or by a recurring rule
            for (const auto &x : store.transactions()) {
                if (x.type != type || x.amount != t.amount || claimed.contains(x.id)) continue;
                if (!x.externalId.isEmpty()) continue; // imported ones are matched by id
                if (std::llabs(x.date.daysTo(t.date)) > 3) continue;
                p.status = Proposed::Status::PossibleDuplicate;
                p.note = QStringLiteral("Possible duplicate of “%1” on %2").arg(x.description.isEmpty() ? store.categoryOrUnknown(x.categoryId).name : x.description, util::fmtDate(x.date));
                p.include = false;
                claimed.insert(x.id);
                break;
            }
            if (p.status == Proposed::Status::New) {
                p.note = sg.source == QLatin1String("rule")      ? QStringLiteral("Category from your rule")
                         : sg.source == QLatin1String("history") ? QStringLiteral("Category from your past choices")
                         : sg.source == QLatin1String("keyword") ? QStringLiteral("Category suggested")
                                                                 : QStringLiteral("Please check the category");
            }
        }
        out << p;
    }
    return out;
}

int commit(Store &store, const QVector<Proposed> &items, QString *error)
{
    QVector<Transaction> txs;
    for (const auto &p : items)
        if (p.include && !p.tx.categoryId.isEmpty()) txs << p.tx;
    return store.importTransactions(txs, error);
}

/* ---------------- Auto-import folders ---------------- */

QString watchFolder(const Store &store, const QString &accountId) { return store.localValue(QStringLiteral("watch:") + accountId); }

void setWatchFolder(Store &store, const QString &accountId, const QString &folder)
{
    store.setLocalValue(QStringLiteral("watch:") + accountId, folder);
}

QVector<AutoImportResult> runAutoImport(Store &store)
{
    QVector<AutoImportResult> results;
    for (const Account &acc : store.accounts()) {
        const QString dir = watchFolder(store, acc.id);
        if (dir.isEmpty() || !QDir(dir).exists()) continue;
        const QStringList files = QDir(dir).entryList({QStringLiteral("*.ofx"), QStringLiteral("*.qfx"), QStringLiteral("*.qbo"), QStringLiteral("*.qif"), QStringLiteral("*.csv")},
                                                      QDir::Files, QDir::Time | QDir::Reversed);
        for (const QString &name : files) {
            const QString path = QDir(dir).filePath(name);
            // skip files still being downloaded / written
            if (QFileInfo(path).lastModified().secsTo(QDateTime::currentDateTime()) < 5) continue;
            AutoImportResult r;
            r.file = name;
            const CsvMapping saved = CsvMapping::fromJson(acc.csvMapping);
            const ParseResult pr = parseFile(path, saved.isValid() ? &saved : nullptr);
            if (!pr.ok()) {
                r.error = pr.error;
                results << r;
                // move it aside so it isn't retried every minute
                QDir().mkpath(QDir(dir).filePath(QStringLiteral("Could not import")));
                QFile::rename(path, QDir(dir).filePath(QStringLiteral("Could not import/") + name));
                continue;
            }
            const auto items = prepare(store, acc, pr.lines);
            QString err;
            r.imported = commit(store, items, &err);
            if (r.imported < 0) {
                r.error = err;
                results << r;
                continue;
            }
            for (const auto &p : items)
                if (!p.include) ++r.skipped;
            results << r;
            QDir().mkpath(QDir(dir).filePath(QStringLiteral("Imported")));
            QString target = QDir(dir).filePath(QStringLiteral("Imported/") + name);
            if (QFile::exists(target))
                target = QDir(dir).filePath(QStringLiteral("Imported/%1-%2").arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")), name));
            QFile::rename(path, target);
        }
    }
    return results;
}

} // namespace bank
