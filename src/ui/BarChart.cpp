#include "BarChart.h"
#include "Theme.h"
#include "core/Util.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <cmath>
#include <algorithm>

namespace {

struct Scale {
    qint64 max;
    qint64 step;
};

Scale niceScale(qint64 max, int ticks = 4)
{
    if (max <= 0) return {10000, 2500};
    const double raw = static_cast<double>(max) / ticks;
    const double mag = std::pow(10.0, std::floor(std::log10(raw)));
    const double norm = raw / mag;
    const double f = norm <= 1 ? 1 : norm <= 2 ? 2 : norm <= 2.5 ? 2.5 : norm <= 5 ? 5 : 10;
    const qint64 step = std::max<qint64>(1, static_cast<qint64>(f * mag));
    return {static_cast<qint64>(std::ceil(static_cast<double>(max) / step)) * step, step};
}

QPainterPath topRounded(const QRectF &r, qreal radius)
{
    radius = std::min({radius, r.width() / 2, r.height()});
    QPainterPath p;
    p.moveTo(r.left(), r.bottom());
    p.lineTo(r.left(), r.top() + radius);
    p.quadTo(r.left(), r.top(), r.left() + radius, r.top());
    p.lineTo(r.right() - radius, r.top());
    p.quadTo(r.right(), r.top(), r.right(), r.top() + radius);
    p.lineTo(r.right(), r.bottom());
    p.closeSubpath();
    return p;
}

} // namespace

BarChart::BarChart(QWidget *parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setMinimumHeight(240);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
}

void BarChart::setData(const QStringList &labels, const QStringList &fullLabels, const QVector<qint64> &income, const QVector<qint64> &expense)
{
    m_labels = labels;
    m_full = fullLabels.isEmpty() ? labels : fullLabels;
    m_income = income;
    m_expense = expense;
    m_hover = -1;
    update();
}

QRectF BarChart::plotRect() const
{
    const QFontMetrics fm(font());
    const int left = fm.horizontalAdvance(util::moneyCompact(99990000)) + 14;
    return QRectF(left, 10, width() - left - 6, height() - 10 - fm.height() - 12);
}

int BarChart::columnAt(const QPointF &pos) const
{
    const QRectF pr = plotRect();
    if (m_labels.isEmpty() || !pr.adjusted(0, 0, 0, 30).contains(pos)) return -1;
    const double band = pr.width() / m_labels.size();
    const int i = static_cast<int>((pos.x() - pr.left()) / band);
    return (i >= 0 && i < m_labels.size()) ? i : -1;
}

void BarChart::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const auto &t = Theme::c();
    const QRectF pr = plotRect();
    const int n = m_labels.size();
    qint64 maxV = 0;
    for (qint64 v : m_income) maxV = std::max(maxV, v);
    for (qint64 v : m_expense) maxV = std::max(maxV, v);
    const Scale sc = niceScale(maxV);
    auto yPos = [&](qint64 v) { return pr.bottom() - (static_cast<double>(v) / sc.max) * pr.height(); };

    QFont small = font();
    small.setPointSizeF(font().pointSizeF() * 0.88);
    p.setFont(small);
    const QFontMetrics fm(small);

    // grid + y labels
    for (qint64 v = 0; v <= sc.max; v += sc.step) {
        const double y = std::round(yPos(v)) + 0.5;
        p.setPen(QPen(v == 0 ? t.axis : t.grid, 1));
        p.drawLine(QPointF(pr.left(), y), QPointF(pr.right(), y));
        p.setPen(t.muted);
        p.drawText(QRectF(0, y - fm.height() / 2.0, pr.left() - 8, fm.height()), Qt::AlignRight | Qt::AlignVCenter, util::moneyCompact(v));
    }
    if (n == 0) return;

    const double band = pr.width() / n;
    const double groupW = std::min(band * 0.72, 64.0);
    const double gap = 2;
    const double barW = std::max(3.0, (groupW - gap) / 2);

    if (m_hover >= 0) p.fillRect(QRectF(pr.left() + band * m_hover, pr.top(), band, pr.height()), t.surface3);

    const int showEvery = band < 30 ? static_cast<int>(std::ceil(30 / band)) : 1;
    for (int i = 0; i < n; ++i) {
        const double gx = pr.left() + band * i + (band - (barW * 2 + gap)) / 2;
        const qint64 vals[2] = {m_income.value(i), m_expense.value(i)};
        const QColor cols[2] = {t.income, t.expense};
        for (int s = 0; s < 2; ++s) {
            if (vals[s] <= 0) continue;
            const double y = yPos(vals[s]);
            p.fillPath(topRounded(QRectF(gx + s * (barW + gap), y, barW, pr.bottom() - y), 4), cols[s]);
        }
        if (i % showEvery == 0) {
            p.setPen(t.muted);
            p.drawText(QRectF(pr.left() + band * i, pr.bottom() + 6, band, fm.height()), Qt::AlignHCenter | Qt::AlignTop, m_labels[i]);
        }
    }

    // tooltip
    if (m_hover >= 0) {
        const qint64 inc = m_income.value(m_hover), exp = m_expense.value(m_hover), net = inc - exp;
        QFont bold = font();
        bold.setWeight(QFont::DemiBold);
        const QFontMetrics bfm(bold);
        const QFontMetrics nfm(font());
        const QString lines[3] = {util::money(inc), util::money(exp), util::signedMoney(net)};
        const QString names[3] = {QStringLiteral("Income"), QStringLiteral("Expenses"), QStringLiteral("Net")};
        int valW = 0;
        for (const auto &l : lines) valW = std::max(valW, bfm.horizontalAdvance(l));
        const int lineH = nfm.height() + 4;
        const double tw = std::max<double>(bfm.horizontalAdvance(m_full.value(m_hover)), 16 + nfm.horizontalAdvance(names[1]) + 16 + valW) + 24;
        const double th = 12 + lineH + lineH * 3 + 8 + 10;
        const double cx = pr.left() + band * m_hover + band / 2;
        double tx = cx + 14;
        if (tx + tw > width() - 2) tx = cx - tw - 14;
        tx = std::max(2.0, tx);
        const QRectF box(tx, pr.top(), tw, th);
        QPainterPath path;
        path.addRoundedRect(box, 10, 10);
        p.fillPath(path, t.surface);
        p.setPen(QPen(t.borderStrong, 1));
        p.drawPath(path);
        double y = box.top() + 10;
        p.setFont(bold);
        p.setPen(t.text);
        p.drawText(QRectF(box.left() + 12, y, tw - 24, lineH), Qt::AlignLeft | Qt::AlignVCenter, m_full.value(m_hover));
        y += lineH + 2;
        for (int k = 0; k < 3; ++k) {
            if (k == 2) {
                p.setPen(QPen(t.border, 1));
                p.drawLine(QPointF(box.left() + 12, y + 2), QPointF(box.right() - 12, y + 2));
                y += 8;
            }
            if (k < 2) {
                QPainterPath sw;
                sw.addRoundedRect(QRectF(box.left() + 12, y + lineH / 2.0 - 5, 10, 10), 3, 3);
                p.fillPath(sw, k == 0 ? t.income : t.expense);
            }
            p.setFont(font());
            p.setPen(t.text2);
            p.drawText(QRectF(box.left() + (k < 2 ? 28 : 12), y, tw, lineH), Qt::AlignLeft | Qt::AlignVCenter, names[k]);
            p.setFont(bold);
            p.setPen(k == 2 && net < 0 ? t.badText : t.text);
            p.drawText(QRectF(box.left(), y, tw - 12, lineH), Qt::AlignRight | Qt::AlignVCenter, lines[k]);
            y += lineH;
        }
    }
}

void BarChart::mouseMoveEvent(QMouseEvent *e)
{
    const int c = columnAt(e->position());
    if (c != m_hover) {
        m_hover = c;
        setCursor(c >= 0 && m_clickable ? Qt::PointingHandCursor : Qt::ArrowCursor);
        update();
    }
}

void BarChart::leaveEvent(QEvent *)
{
    m_hover = -1;
    update();
}

void BarChart::mousePressEvent(QMouseEvent *e)
{
    const int c = columnAt(e->position());
    if (c >= 0 && m_clickable && e->button() == Qt::LeftButton) emit columnClicked(c);
}
