#include "Xlsx.h"

#include <QDateTime>
#include <QHash>
#include <QSaveFile>
#include <QSet>
#include <QtEndian>
#include <algorithm>
#include <array>
#include <cmath>

namespace xlsx {

/* ------------------------------------------------------------------ */
/* Zip container (deflate via zlib through qCompress)                 */
/* ------------------------------------------------------------------ */
namespace {

quint32 crc32(const QByteArray &data)
{
    static const std::array<quint32, 256> table = [] {
        std::array<quint32, 256> t{};
        for (quint32 i = 0; i < 256; ++i) {
            quint32 c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[i] = c;
        }
        return t;
    }();
    quint32 c = 0xFFFFFFFFu;
    for (const char ch : data) c = table[(c ^ static_cast<quint8>(ch)) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

void put16(QByteArray &b, quint16 v)
{
    char buf[2];
    qToLittleEndian(v, buf);
    b.append(buf, 2);
}
void put32(QByteArray &b, quint32 v)
{
    char buf[4];
    qToLittleEndian(v, buf);
    b.append(buf, 4);
}

class ZipWriter {
public:
    void add(const QString &name, const QByteArray &data)
    {
        const QByteArray fname = name.toUtf8();
        const quint32 crc = crc32(data);
        // qCompress = 4-byte length + zlib stream (2-byte header, raw deflate, 4-byte adler32)
        const QByteArray z = qCompress(data, 9);
        QByteArray deflated = z.size() > 10 ? z.mid(6, z.size() - 10) : QByteArray();
        quint16 method = 8;
        if (deflated.isEmpty() || deflated.size() >= data.size()) {
            deflated = data;
            method = 0; // stored
        }
        const QDateTime now = QDateTime::currentDateTime();
        const quint16 dosTime = static_cast<quint16>((now.time().hour() << 11) | (now.time().minute() << 5) | (now.time().second() / 2));
        const quint16 dosDate = static_cast<quint16>(((now.date().year() - 1980) << 9) | (now.date().month() << 5) | now.date().day());

        Entry e{fname, crc, static_cast<quint32>(deflated.size()), static_cast<quint32>(data.size()), static_cast<quint32>(m_out.size()), method, dosTime, dosDate};
        put32(m_out, 0x04034b50);
        put16(m_out, 20);     // version needed
        put16(m_out, 0x0800); // flags: UTF-8 names
        put16(m_out, method);
        put16(m_out, dosTime);
        put16(m_out, dosDate);
        put32(m_out, crc);
        put32(m_out, e.csize);
        put32(m_out, e.usize);
        put16(m_out, static_cast<quint16>(fname.size()));
        put16(m_out, 0);
        m_out.append(fname);
        m_out.append(deflated);
        m_entries.push_back(e);
    }

    QByteArray finish()
    {
        const quint32 cdStart = static_cast<quint32>(m_out.size());
        for (const Entry &e : m_entries) {
            put32(m_out, 0x02014b50);
            put16(m_out, 20); // version made by
            put16(m_out, 20); // version needed
            put16(m_out, 0x0800);
            put16(m_out, e.method);
            put16(m_out, e.time);
            put16(m_out, e.date);
            put32(m_out, e.crc);
            put32(m_out, e.csize);
            put32(m_out, e.usize);
            put16(m_out, static_cast<quint16>(e.name.size()));
            put16(m_out, 0); // extra
            put16(m_out, 0); // comment
            put16(m_out, 0); // disk
            put16(m_out, 0); // internal attrs
            put32(m_out, 0); // external attrs
            put32(m_out, e.offset);
            m_out.append(e.name);
        }
        const quint32 cdSize = static_cast<quint32>(m_out.size()) - cdStart;
        put32(m_out, 0x06054b50);
        put16(m_out, 0);
        put16(m_out, 0);
        put16(m_out, static_cast<quint16>(m_entries.size()));
        put16(m_out, static_cast<quint16>(m_entries.size()));
        put32(m_out, cdSize);
        put32(m_out, cdStart);
        put16(m_out, 0);
        return m_out;
    }

private:
    struct Entry {
        QByteArray name;
        quint32 crc, csize, usize, offset;
        quint16 method, time, date;
    };
    QByteArray m_out;
    QVector<Entry> m_entries;
};

/* ------------------------------------------------------------------ */
/* XML helpers                                                         */
/* ------------------------------------------------------------------ */

QString xmlEscape(const QString &s)
{
    QString out;
    out.reserve(s.size() + 8);
    for (const QChar c : s) {
        const ushort u = c.unicode();
        if (u == '&') out += QStringLiteral("&amp;");
        else if (u == '<') out += QStringLiteral("&lt;");
        else if (u == '>') out += QStringLiteral("&gt;");
        else if (u == '"') out += QStringLiteral("&quot;");
        else if (u < 0x20 && u != '\t' && u != '\n' && u != '\r') continue; // invalid in XML 1.0
        else if (u == 0xFFFE || u == 0xFFFF) continue;
        else out += c;
    }
    return out;
}

const char *kXmlHeader = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n";

/* Distinct style combinations are collected into cellXfs. */
struct StyleKey {
    Format format;
    bool bold, italic, muted, title;
    Fill fill;
    Align align;
    bool operator==(const StyleKey &o) const
    {
        return format == o.format && bold == o.bold && italic == o.italic && muted == o.muted && title == o.title && fill == o.fill && align == o.align;
    }
};
size_t qHash(const StyleKey &k, size_t seed = 0)
{
    return ::qHash((static_cast<int>(k.align) << 12) | (static_cast<int>(k.format) << 8) | (k.bold << 7) | (k.italic << 6) | (k.muted << 5) |
                       (k.title << 4) | static_cast<int>(k.fill),
                   seed);
}

double excelSerial(const QDate &d) { return static_cast<double>(QDate(1899, 12, 30).daysTo(d)); }

QString formatNumber(double v)
{
    if (std::fabs(v - std::round(v)) < 1e-9 && std::fabs(v) < 1e15) return QString::number(static_cast<qint64>(std::llround(v)));
    return QString::number(v, 'g', 15);
}

} // namespace

QString columnName(int index)
{
    QString s;
    ++index;
    while (index > 0) {
        const int rem = (index - 1) % 26;
        s.prepend(QChar('A' + rem));
        index = (index - 1) / 26;
    }
    return s;
}

QString sanitizeSheetName(const QString &name)
{
    QString n;
    for (const QChar c : name)
        if (!QStringLiteral("[]:*?/\\").contains(c)) n += c;
    n = n.trimmed();
    if (n.startsWith(QLatin1Char('\''))) n.remove(0, 1);
    if (n.isEmpty()) n = QStringLiteral("Sheet");
    return n.left(31);
}

void Sheet::autoSizeColumns(double min, double max)
{
    int cols = 0;
    for (const auto &r : rows) cols = std::max(cols, static_cast<int>(r.size()));
    columnWidths.resize(cols);
    QVector<double> w(cols, min);
    for (const auto &r : rows) {
        for (int c = 0; c < r.size(); ++c) {
            const Cell &cell = r[c];
            if (cell.title) continue; // headings may overflow
            double len = 0;
            switch (cell.type) {
            case Cell::Type::Text: len = cell.text.size(); break;
            case Cell::Type::Number:
                len = formatNumber(std::round(cell.number)).size();
                if (cell.format == Format::Money || cell.format == Format::MoneyDash) len = std::max(10.0, len * 1.35 + 5);
                else len += 2;
                break;
            case Cell::Type::Date: len = 12; break;
            default: break;
            }
            if (cell.bold) len *= 1.1;
            w[c] = std::max(w[c], len + 2);
        }
    }
    for (int c = 0; c < cols; ++c)
        if (columnWidths[c] <= 0) columnWidths[c] = std::min(max, w[c]);
}

Workbook::Workbook(const QString &currencySymbol, int decimals)
    : m_symbol(currencySymbol)
    , m_decimals(decimals)
{
}

Sheet &Workbook::addSheet(const QString &name)
{
    QString base = sanitizeSheetName(name);
    QString n = base;
    int i = 2;
    auto taken = [this](const QString &x) {
        for (const auto &s : m_sheets)
            if (s.name.compare(x, Qt::CaseInsensitive) == 0) return true;
        return false;
    };
    while (taken(n)) {
        const QString suffix = QStringLiteral(" (%1)").arg(i++);
        n = base.left(31 - suffix.size()) + suffix;
    }
    Sheet s;
    s.name = n;
    m_sheets.push_back(s);
    return m_sheets.last();
}

QByteArray Workbook::toBytes() const
{
    // ---- styles ----
    QVector<StyleKey> styles;
    QHash<StyleKey, int> styleIndex;
    styles.push_back({Format::General, false, false, false, false, Fill::None, Align::Auto}); // xf 0 = default
    styleIndex.insert(styles[0], 0);
    auto styleOf = [&](const Cell &c) {
        StyleKey k{c.format, c.bold, c.italic, c.muted, c.title, c.fill, c.align};
        auto it = styleIndex.find(k);
        if (it != styleIndex.end()) return *it;
        const int idx = styles.size();
        styles.push_back(k);
        styleIndex.insert(k, idx);
        return idx;
    };

    // ---- sheets ----
    QVector<QByteArray> sheetXml;
    const QVector<Sheet> sheets = m_sheets.isEmpty() ? QVector<Sheet>{Sheet{QStringLiteral("Sheet1"), {}, {}, 0, -1, -1}} : m_sheets;
    for (const Sheet &sh : sheets) {
        QString x = QString::fromLatin1(kXmlHeader);
        x += QStringLiteral("<worksheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" "
                            "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\">");
        int maxCols = 0;
        for (const auto &r : sh.rows) maxCols = std::max(maxCols, static_cast<int>(r.size()));
        if (maxCols > 0 && !sh.rows.isEmpty())
            x += QStringLiteral("<dimension ref=\"A1:%1%2\"/>").arg(columnName(maxCols - 1)).arg(sh.rows.size());
        x += QStringLiteral("<sheetViews><sheetView workbookViewId=\"0\"%1>").arg(&sh == &sheets.first() ? QStringLiteral(" tabSelected=\"1\"") : QString());
        if (sh.frozenRows > 0)
            x += QStringLiteral("<pane ySplit=\"%1\" topLeftCell=\"A%2\" activePane=\"bottomLeft\" state=\"frozen\"/>"
                                "<selection pane=\"bottomLeft\" activeCell=\"A%2\" sqref=\"A%2\"/>")
                     .arg(sh.frozenRows)
                     .arg(sh.frozenRows + 1);
        x += QStringLiteral("</sheetView></sheetViews><sheetFormatPr defaultRowHeight=\"15\"/>");
        if (!sh.columnWidths.isEmpty()) {
            x += QStringLiteral("<cols>");
            for (int c = 0; c < sh.columnWidths.size(); ++c)
                if (sh.columnWidths[c] > 0)
                    x += QStringLiteral("<col min=\"%1\" max=\"%1\" width=\"%2\" customWidth=\"1\"/>").arg(c + 1).arg(sh.columnWidths[c], 0, 'f', 1);
            x += QStringLiteral("</cols>");
        }
        x += QStringLiteral("<sheetData>");
        for (int r = 0; r < sh.rows.size(); ++r) {
            const auto &row = sh.rows[r];
            bool isTitle = false;
            for (const auto &c : row) isTitle = isTitle || c.title;
            x += QStringLiteral("<row r=\"%1\"%2>").arg(r + 1).arg(isTitle ? QStringLiteral(" ht=\"22\" customHeight=\"1\"") : QString());
            for (int c = 0; c < row.size(); ++c) {
                const Cell &cell = row[c];
                const QString ref = columnName(c) + QString::number(r + 1);
                const int s = styleOf(cell);
                const QString sAttr = s ? QStringLiteral(" s=\"%1\"").arg(s) : QString();
                switch (cell.type) {
                case Cell::Type::Empty:
                    if (s) x += QStringLiteral("<c r=\"%1\"%2/>").arg(ref, sAttr);
                    break;
                case Cell::Type::Text:
                    x += QStringLiteral("<c r=\"%1\" t=\"inlineStr\"%2><is><t xml:space=\"preserve\">%3</t></is></c>").arg(ref, sAttr, xmlEscape(cell.text));
                    break;
                case Cell::Type::Number:
                    x += QStringLiteral("<c r=\"%1\"%2><v>%3</v></c>").arg(ref, sAttr, formatNumber(cell.number));
                    break;
                case Cell::Type::Date:
                    if (cell.date.isValid()) x += QStringLiteral("<c r=\"%1\"%2><v>%3</v></c>").arg(ref, sAttr, formatNumber(excelSerial(cell.date)));
                    break;
                }
            }
            x += QStringLiteral("</row>");
        }
        x += QStringLiteral("</sheetData>");
        if (sh.autoFilterRow >= 0 && maxCols > 0) {
            const int last = sh.autoFilterLastRow >= 0 ? sh.autoFilterLastRow : sh.rows.size() - 1;
            x += QStringLiteral("<autoFilter ref=\"A%1:%2%3\"/>").arg(sh.autoFilterRow + 1).arg(columnName(maxCols - 1)).arg(last + 1);
        }
        x += QStringLiteral("<pageMargins left=\"0.5\" right=\"0.5\" top=\"0.6\" bottom=\"0.6\" header=\"0.3\" footer=\"0.3\"/>"
                            "<pageSetup orientation=\"landscape\" fitToWidth=\"1\" fitToHeight=\"0\"/>"
                            "</worksheet>");
        sheetXml.push_back(x.toUtf8());
    }

    // ---- styles.xml ----
    const QString dec = m_decimals > 0 ? QStringLiteral(".") + QString(m_decimals, QLatin1Char('0')) : QString();
    const QString sym = xmlEscape(QString(m_symbol).replace(QLatin1Char('"'), QString()));
    const QString money = QStringLiteral("&quot;%1&quot;#,##0%2").arg(sym, dec);
    struct NumFmt {
        Format f;
        int id;
        QString code;
    };
    const QVector<NumFmt> numFmts = {
        {Format::Money, 164, QStringLiteral("%1;[Red]-%1").arg(money)},
        {Format::MoneyDash, 165, QStringLiteral("%1;[Red]-%1;&quot;–&quot;").arg(money)},
        {Format::Percent, 166, QStringLiteral("0.0%")},
        {Format::Change, 167, QStringLiteral("&quot;▲ &quot;0.0%;[Red]&quot;▼ &quot;0.0%;0.0%")},
        {Format::Date, 168, QStringLiteral("d mmm yyyy")},
    };
    auto numFmtId = [&](Format f) {
        if (f == Format::General) return 0;
        if (f == Format::Integer) return 3; // #,##0
        for (const auto &n : numFmts)
            if (n.f == f) return n.id;
        return 0;
    };
    // fonts: 0 normal, 1 bold, 2 italic, 3 muted, 4 title, 5 bold+italic
    auto fontId = [](const StyleKey &k) {
        if (k.title) return 4;
        if (k.bold && k.italic) return 5;
        if (k.bold) return 1;
        if (k.italic) return 2;
        if (k.muted) return 3;
        return 0;
    };
    // fills: 0 none, 1 gray125 (required), 2 header, 3 section, 4 total, 5 net
    auto fillId = [](Fill f) {
        switch (f) {
        case Fill::Header: return 2;
        case Fill::Section: return 3;
        case Fill::Total: return 4;
        case Fill::Net: return 5;
        default: return 0;
        }
    };
    QString st = QString::fromLatin1(kXmlHeader);
    st += QStringLiteral("<styleSheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\">");
    st += QStringLiteral("<numFmts count=\"%1\">").arg(numFmts.size());
    for (const auto &n : numFmts) st += QStringLiteral("<numFmt numFmtId=\"%1\" formatCode=\"%2\"/>").arg(n.id).arg(n.code);
    st += QStringLiteral("</numFmts>");
    st += QStringLiteral("<fonts count=\"6\">"
                         "<font><sz val=\"11\"/><name val=\"Calibri\"/><family val=\"2\"/></font>"
                         "<font><b/><sz val=\"11\"/><name val=\"Calibri\"/><family val=\"2\"/></font>"
                         "<font><i/><sz val=\"11\"/><name val=\"Calibri\"/><family val=\"2\"/></font>"
                         "<font><sz val=\"10\"/><color rgb=\"FF6F6E69\"/><name val=\"Calibri\"/><family val=\"2\"/></font>"
                         "<font><b/><sz val=\"15\"/><color rgb=\"FF101826\"/><name val=\"Calibri\"/><family val=\"2\"/></font>"
                         "<font><b/><i/><sz val=\"11\"/><name val=\"Calibri\"/><family val=\"2\"/></font>"
                         "</fonts>");
    st += QStringLiteral("<fills count=\"6\">"
                         "<fill><patternFill patternType=\"none\"/></fill>"
                         "<fill><patternFill patternType=\"gray125\"/></fill>"
                         "<fill><patternFill patternType=\"solid\"><fgColor rgb=\"FFE7EEF8\"/><bgColor indexed=\"64\"/></patternFill></fill>"
                         "<fill><patternFill patternType=\"solid\"><fgColor rgb=\"FFF0EFEC\"/><bgColor indexed=\"64\"/></patternFill></fill>"
                         "<fill><patternFill patternType=\"solid\"><fgColor rgb=\"FFF9F9F7\"/><bgColor indexed=\"64\"/></patternFill></fill>"
                         "<fill><patternFill patternType=\"solid\"><fgColor rgb=\"FFE6F0FC\"/><bgColor indexed=\"64\"/></patternFill></fill>"
                         "</fills>");
    // borders: 0 none, 1 bottom (header), 2 top (totals)
    st += QStringLiteral("<borders count=\"3\">"
                         "<border><left/><right/><top/><bottom/><diagonal/></border>"
                         "<border><left/><right/><top/><bottom style=\"thin\"><color rgb=\"FF9A9890\"/></bottom><diagonal/></border>"
                         "<border><left/><right/><top style=\"thin\"><color rgb=\"FF9A9890\"/></top><bottom/><diagonal/></border>"
                         "</borders>");
    st += QStringLiteral("<cellStyleXfs count=\"1\"><xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"0\"/></cellStyleXfs>");
    st += QStringLiteral("<cellXfs count=\"%1\">").arg(styles.size());
    for (const StyleKey &k : styles) {
        const int nf = numFmtId(k.format);
        const int border = k.fill == Fill::Header ? 1 : (k.fill == Fill::Total || k.fill == Fill::Net) ? 2 : 0;
        const QString align = k.align == Align::Left ? QStringLiteral("left") : k.align == Align::Right ? QStringLiteral("right")
                            : k.align == Align::Center                         ? QStringLiteral("center")
                                                                               : QString();
        st += QStringLiteral("<xf numFmtId=\"%1\" fontId=\"%2\" fillId=\"%3\" borderId=\"%4\" xfId=\"0\"%5%6%7%8%9")
                  .arg(nf)
                  .arg(fontId(k))
                  .arg(fillId(k.fill))
                  .arg(border)
                  .arg(nf ? QStringLiteral(" applyNumberFormat=\"1\"") : QString())
                  .arg(fontId(k) ? QStringLiteral(" applyFont=\"1\"") : QString())
                  .arg(fillId(k.fill) ? QStringLiteral(" applyFill=\"1\"") : QString())
                  .arg(border ? QStringLiteral(" applyBorder=\"1\"") : QString())
                  .arg(align.isEmpty() ? QString() : QStringLiteral(" applyAlignment=\"1\""));
        st += align.isEmpty() ? QStringLiteral("/>") : QStringLiteral("><alignment horizontal=\"%1\"/></xf>").arg(align);
    }
    st += QStringLiteral("</cellXfs><cellStyles count=\"1\"><cellStyle name=\"Normal\" xfId=\"0\" builtinId=\"0\"/></cellStyles>"
                         "<dxfs count=\"0\"/><tableStyles count=\"0\"/></styleSheet>");

    // ---- workbook & package parts ----
    QString wb = QString::fromLatin1(kXmlHeader);
    wb += QStringLiteral("<workbook xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" "
                         "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\">"
                         "<bookViews><workbookView/></bookViews><sheets>");
    for (int i = 0; i < sheets.size(); ++i)
        wb += QStringLiteral("<sheet name=\"%1\" sheetId=\"%2\" r:id=\"rId%2\"/>").arg(xmlEscape(sheets[i].name)).arg(i + 1);
    wb += QStringLiteral("</sheets><definedNames>");
    for (int i = 0; i < sheets.size(); ++i) {
        const Sheet &sh = sheets[i];
        if (sh.autoFilterRow < 0) continue;
        int maxCols = 0;
        for (const auto &r : sh.rows) maxCols = std::max(maxCols, static_cast<int>(r.size()));
        const int last = sh.autoFilterLastRow >= 0 ? sh.autoFilterLastRow : sh.rows.size() - 1;
        wb += QStringLiteral("<definedName name=\"_xlnm._FilterDatabase\" localSheetId=\"%1\" hidden=\"1\">'%2'!$A$%3:$%4$%5</definedName>")
                  .arg(i)
                  .arg(xmlEscape(QString(sh.name).replace(QLatin1Char('\''), QStringLiteral("''"))))
                  .arg(sh.autoFilterRow + 1)
                  .arg(columnName(maxCols - 1))
                  .arg(last + 1);
    }
    wb += QStringLiteral("</definedNames></workbook>");
    wb.replace(QStringLiteral("<definedNames></definedNames>"), QString());

    QString wbRels = QString::fromLatin1(kXmlHeader);
    wbRels += QStringLiteral("<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">");
    for (int i = 0; i < sheets.size(); ++i)
        wbRels += QStringLiteral("<Relationship Id=\"rId%1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet\" "
                                 "Target=\"worksheets/sheet%1.xml\"/>")
                      .arg(i + 1);
    wbRels += QStringLiteral("<Relationship Id=\"rId%1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles\" "
                             "Target=\"styles.xml\"/></Relationships>")
                  .arg(sheets.size() + 1);

    QString ct = QString::fromLatin1(kXmlHeader);
    ct += QStringLiteral("<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
                         "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
                         "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
                         "<Override PartName=\"/xl/workbook.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml\"/>"
                         "<Override PartName=\"/xl/styles.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.styles+xml\"/>"
                         "<Override PartName=\"/docProps/core.xml\" ContentType=\"application/vnd.openxmlformats-package.core-properties+xml\"/>"
                         "<Override PartName=\"/docProps/app.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.extended-properties+xml\"/>");
    for (int i = 0; i < sheets.size(); ++i)
        ct += QStringLiteral("<Override PartName=\"/xl/worksheets/sheet%1.xml\" "
                             "ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml\"/>")
                  .arg(i + 1);
    ct += QStringLiteral("</Types>");

    const QString rels = QString::fromLatin1(kXmlHeader) +
                         QStringLiteral("<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
                                        "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"xl/workbook.xml\"/>"
                                        "<Relationship Id=\"rId2\" Type=\"http://schemas.openxmlformats.org/package/2006/relationships/metadata/core-properties\" Target=\"docProps/core.xml\"/>"
                                        "<Relationship Id=\"rId3\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/extended-properties\" Target=\"docProps/app.xml\"/>"
                                        "</Relationships>");
    const QString nowIso = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    const QString core = QString::fromLatin1(kXmlHeader) +
                         QStringLiteral("<cp:coreProperties xmlns:cp=\"http://schemas.openxmlformats.org/package/2006/metadata/core-properties\" "
                                        "xmlns:dc=\"http://purl.org/dc/elements/1.1/\" xmlns:dcterms=\"http://purl.org/dc/terms/\" "
                                        "xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\">"
                                        "<dc:creator>Ledgerly</dc:creator>"
                                        "<dcterms:created xsi:type=\"dcterms:W3CDTF\">%1</dcterms:created>"
                                        "<dcterms:modified xsi:type=\"dcterms:W3CDTF\">%1</dcterms:modified>"
                                        "</cp:coreProperties>")
                             .arg(nowIso);
    const QString app = QString::fromLatin1(kXmlHeader) +
                        QStringLiteral("<Properties xmlns=\"http://schemas.openxmlformats.org/officeDocument/2006/extended-properties\">"
                                       "<Application>Ledgerly</Application></Properties>");

    ZipWriter zip;
    zip.add(QStringLiteral("[Content_Types].xml"), ct.toUtf8());
    zip.add(QStringLiteral("_rels/.rels"), rels.toUtf8());
    zip.add(QStringLiteral("docProps/core.xml"), core.toUtf8());
    zip.add(QStringLiteral("docProps/app.xml"), app.toUtf8());
    zip.add(QStringLiteral("xl/workbook.xml"), wb.toUtf8());
    zip.add(QStringLiteral("xl/_rels/workbook.xml.rels"), wbRels.toUtf8());
    zip.add(QStringLiteral("xl/styles.xml"), st.toUtf8());
    for (int i = 0; i < sheetXml.size(); ++i) zip.add(QStringLiteral("xl/worksheets/sheet%1.xml").arg(i + 1), sheetXml[i]);
    return zip.finish();
}

bool Workbook::save(const QString &path, QString *error) const
{
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        if (error) *error = f.errorString();
        return false;
    }
    f.write(toBytes());
    if (!f.commit()) {
        if (error) *error = f.errorString();
        return false;
    }
    return true;
}

} // namespace xlsx
