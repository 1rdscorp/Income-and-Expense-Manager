#include "Widgets.h"
#include "Theme.h"

#include <QButtonGroup>
#include <QDir>
#include <QFileDialog>
#include <QStandardPaths>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QPainter>
#include <QPainterPath>
#include <QScrollBar>
#include <QTableWidget>
#include <QVBoxLayout>
#include <algorithm>

/* ---------------- Card ---------------- */

Card::Card(const QString &title, const QString &subtitle, QWidget *parent)
    : QFrame(parent)
{
    setObjectName(QStringLiteral("card"));
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(18, 16, 18, 16);
    outer->setSpacing(12);
    m_header = new QHBoxLayout;
    m_header->setSpacing(8);
    m_title = new QLabel(title);
    m_title->setObjectName(QStringLiteral("cardTitle"));
    m_sub = new QLabel(subtitle);
    m_sub->setObjectName(QStringLiteral("cardSub"));
    m_header->addWidget(m_title);
    m_header->addWidget(m_sub);
    m_header->addStretch(1);
    m_title->setVisible(!title.isEmpty());
    m_sub->setVisible(!subtitle.isEmpty());
    outer->addLayout(m_header);
    m_body = new QVBoxLayout;
    m_body->setSpacing(8);
    outer->addLayout(m_body, 1);
}

void Card::setTitle(const QString &title)
{
    m_title->setText(title);
    m_title->setVisible(!title.isEmpty());
}

void Card::setSubtitle(const QString &sub)
{
    m_sub->setText(sub);
    m_sub->setVisible(!sub.isEmpty());
}

/* ---------------- KpiCard ---------------- */

KpiCard::KpiCard(QWidget *parent)
    : QFrame(parent)
{
    setObjectName(QStringLiteral("card"));
    auto *l = new QVBoxLayout(this);
    l->setContentsMargins(16, 14, 16, 14);
    l->setSpacing(4);
    m_label = new QLabel;
    m_label->setObjectName(QStringLiteral("kpiLabel"));
    m_label->setTextFormat(Qt::RichText);
    m_value = new QLabel;
    m_value->setObjectName(QStringLiteral("kpiValue"));
    m_value->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_meta = new QLabel;
    m_meta->setObjectName(QStringLiteral("kpiMeta"));
    m_meta->setTextFormat(Qt::RichText);
    m_meta->setWordWrap(true);
    l->addWidget(m_label);
    l->addWidget(m_value);
    l->addWidget(m_meta);
    l->addStretch(1);
    setMinimumWidth(170);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
}

void KpiCard::set(const QString &label, const QString &value, const QString &metaHtml, const QColor &swatch, bool negative)
{
    QString lbl = label.toHtmlEscaped();
    if (swatch.isValid())
        lbl = QStringLiteral("<span style='color:%1'>■</span>&nbsp;%2").arg(swatch.name(), lbl);
    m_label->setText(lbl);
    m_value->setText(value);
    QPalette p = m_value->palette();
    p.setColor(QPalette::WindowText, negative ? Theme::c().badText : Theme::c().text);
    m_value->setPalette(p);
    m_value->setStyleSheet(negative ? QStringLiteral("color:%1").arg(Theme::c().badText.name()) : QString());
    m_meta->setText(metaHtml);
    m_meta->setVisible(!metaHtml.isEmpty());
}

/* ---------------- SegmentedControl ---------------- */

SegmentedControl::SegmentedControl(const QVector<QPair<QString, QString>> &options, QWidget *parent)
    : QFrame(parent)
{
    setObjectName(QStringLiteral("seg"));
    auto *l = new QHBoxLayout(this);
    l->setContentsMargins(3, 3, 3, 3);
    l->setSpacing(2);
    m_group = new QButtonGroup(this);
    m_group->setExclusive(true);
    for (const auto &o : options) {
        auto *b = new QPushButton(o.second);
        b->setCheckable(true);
        b->setCursor(Qt::PointingHandCursor);
        b->setProperty("value", o.first);
        m_group->addButton(b);
        m_buttons << b;
        l->addWidget(b);
        connect(b, &QPushButton::clicked, this, [this, b]() {
            const QString v = b->property("value").toString();
            if (v == m_value) return;
            m_value = v;
            emit changed(v);
        });
    }
    if (!options.isEmpty()) setValue(options.first().first);
    setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
}

void SegmentedControl::setValue(const QString &v)
{
    m_value = v;
    for (auto *b : m_buttons) b->setChecked(b->property("value").toString() == v);
}

void SegmentedControl::setLabel(const QString &value, const QString &label)
{
    for (auto *b : m_buttons)
        if (b->property("value").toString() == value) b->setText(label);
}

/* ---------------- PeriodNav ---------------- */

PeriodNav::PeriodNav(QWidget *parent)
    : QWidget(parent)
{
    auto *l = new QHBoxLayout(this);
    l->setContentsMargins(0, 0, 0, 0);
    l->setSpacing(4);
    m_prev = new QPushButton;
    m_next = new QPushButton;
    m_prev->setIcon(Theme::icon(QStringLiteral("left")));
    m_next->setIcon(Theme::icon(QStringLiteral("right")));
    m_prev->setToolTip(QStringLiteral("Previous"));
    m_next->setToolTip(QStringLiteral("Next"));
    m_label = new QLabel;
    m_label->setAlignment(Qt::AlignCenter);
    m_label->setMinimumWidth(140);
    m_label->setStyleSheet(QStringLiteral("font-weight:600;"));
    l->addWidget(m_prev);
    l->addWidget(m_label);
    l->addWidget(m_next);
    connect(m_prev, &QPushButton::clicked, this, [this]() { emit stepped(-1); });
    connect(m_next, &QPushButton::clicked, this, [this]() { emit stepped(1); });
    connect(&Theme::instance(), &Theme::changed, this, [this]() {
        m_prev->setIcon(Theme::icon(QStringLiteral("left")));
        m_next->setIcon(Theme::icon(QStringLiteral("right")));
    });
}

void PeriodNav::setLabel(const QString &text) { m_label->setText(text); }

/* ---------------- HBarList ---------------- */

HBarList::HBarList(QWidget *parent)
    : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

int HBarList::rowHeight(const Row &r) const
{
    const int line = fontMetrics().height();
    return line + 6 + 8 + ((r.sub.isEmpty() && r.warn.isEmpty()) ? 0 : 4 + line - 2) + 12;
}

QSize HBarList::sizeHint() const
{
    int h = 0;
    for (const auto &r : m_rows) h += rowHeight(r);
    if (m_rows.isEmpty()) h = fontMetrics().height() * 3;
    return QSize(300, h);
}

QSize HBarList::minimumSizeHint() const { return QSize(200, sizeHint().height()); }

void HBarList::setRows(const QVector<Row> &rows, const QString &emptyText)
{
    m_rows = rows;
    m_empty = emptyText;
    setFixedHeight(sizeHint().height());
    updateGeometry();
    update();
}

void HBarList::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const auto &t = Theme::c();
    const int w = width();
    if (m_rows.isEmpty()) {
        p.setPen(t.muted);
        p.drawText(rect(), Qt::AlignCenter, m_empty);
        return;
    }
    QFont base = font();
    QFont bold = base;
    bold.setWeight(QFont::DemiBold);
    QFont small = base;
    small.setPointSizeF(base.pointSizeF() * 0.85);
    const int line = fontMetrics().height();
    int y = 0;
    for (const auto &r : m_rows) {
        // top line
        int x = 0;
        p.setFont(base);
        if (!r.icon.isEmpty()) {
            p.setPen(t.text);
            p.drawText(QRect(0, y, 24, line), Qt::AlignLeft | Qt::AlignVCenter, r.icon);
            x = 26;
        }
        p.setFont(bold);
        const int amtW = QFontMetrics(bold).horizontalAdvance(r.amount);
        p.setPen(t.text);
        p.drawText(QRect(w - amtW, y, amtW, line), Qt::AlignRight | Qt::AlignVCenter, r.amount);
        int right = w - amtW - 10;
        if (!r.extra.isEmpty()) {
            p.setFont(small);
            const int ew = QFontMetrics(small).horizontalAdvance(r.extra);
            p.setPen(t.goodText);
            p.drawText(QRect(right - ew, y, ew, line), Qt::AlignRight | Qt::AlignVCenter, r.extra);
            right -= ew + 10;
        }
        p.setFont(base);
        p.setPen(t.text);
        const QString name = QFontMetrics(base).elidedText(r.name, Qt::ElideRight, std::max(10, right - x));
        p.drawText(QRect(x, y, right - x, line), Qt::AlignLeft | Qt::AlignVCenter, name);
        y += line + 6;
        // bar
        QPainterPath track;
        track.addRoundedRect(QRectF(0, y, w, 8), 4, 4);
        p.fillPath(track, t.surface3);
        if (r.fraction > 0) {
            const double fw = std::max(3.0, std::min(1.0, r.fraction) * w);
            QPainterPath fill;
            fill.addRoundedRect(QRectF(0, y, fw, 8), 4, 4);
            p.fillPath(fill, r.over ? t.critical : t.expense);
        }
        y += 8;
        if (!r.sub.isEmpty() || !r.warn.isEmpty()) {
            y += 4;
            p.setFont(small);
            int sx = 0;
            if (!r.sub.isEmpty()) {
                p.setPen(t.muted);
                p.drawText(QRect(0, y, w, line), Qt::AlignLeft | Qt::AlignVCenter, r.sub);
                sx = QFontMetrics(small).horizontalAdvance(r.sub) + 8;
            }
            if (!r.warn.isEmpty()) {
                QFont sb = small;
                sb.setWeight(QFont::DemiBold);
                p.setFont(sb);
                p.setPen(r.over ? t.badText : t.muted);
                p.drawText(QRect(sx, y, w - sx, line), Qt::AlignLeft | Qt::AlignVCenter,
                           QFontMetrics(sb).elidedText(r.warn, Qt::ElideRight, std::max(10, w - sx)));
            }
            y += line - 2;
        }
        y += 12;
    }
}

/* ---------------- Banner ---------------- */

Banner::Banner(QWidget *parent)
    : QFrame(parent)
{
    setObjectName(QStringLiteral("banner"));
    auto *l = new QHBoxLayout(this);
    l->setContentsMargins(14, 10, 14, 10);
    l->setSpacing(12);
    m_icon = new QLabel;
    m_icon->setPixmap(Theme::iconPixmap(QStringLiteral("alert"), QColor(0xa86e00), 20));
    m_text = new QLabel;
    m_text->setWordWrap(true);
    m_text->setTextFormat(Qt::RichText);
    m_button = new QPushButton;
    l->addWidget(m_icon, 0, Qt::AlignTop);
    l->addWidget(m_text, 1);
    l->addWidget(m_button, 0, Qt::AlignVCenter);
}

void Banner::setText(const QString &html) { m_text->setText(html); }

/* ---------------- helpers ---------------- */

namespace ui {

QPushButton *button(const QString &text, const QString &icon, const QString &objectName, QWidget *parent)
{
    auto *b = new QPushButton(text, parent);
    if (!objectName.isEmpty()) b->setObjectName(objectName);
    if (!icon.isEmpty()) {
        const QColor col = (objectName == QLatin1String("primary") || objectName == QLatin1String("danger")) ? QColor(Qt::white) : QColor();
        b->setIcon(Theme::icon(icon, col));
        b->setProperty("iconName", icon);
    }
    b->setCursor(Qt::PointingHandCursor);
    return b;
}

QLabel *muted(const QString &text, QWidget *parent)
{
    auto *l = new QLabel(text, parent);
    l->setObjectName(QStringLiteral("muted"));
    l->setWordWrap(true);
    return l;
}

void clearLayout(QLayout *layout)
{
    if (!layout) return;
    while (QLayoutItem *item = layout->takeAt(0)) {
        if (QWidget *w = item->widget()) w->deleteLater();
        if (QLayout *child = item->layout()) clearLayout(child);
        delete item;
    }
}

void fitTableHeight(QTableWidget *table)
{
    table->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    table->resizeRowsToContents();
    int h = table->horizontalHeader()->isVisible() ? table->horizontalHeader()->sizeHint().height() : 0;
    for (int r = 0; r < table->rowCount(); ++r) h += table->rowHeight(r);
    if (table->horizontalScrollBar()->isVisible() || table->horizontalHeader()->length() > table->viewport()->width())
        h += table->horizontalScrollBar()->sizeHint().height();
    table->setFixedHeight(h + 2 * table->frameWidth() + 2);
}

QString askExportPath(QWidget *parent, const QString &title, const QString &baseName, bool excelDefault)
{
    const QString excel = QStringLiteral("Excel workbook (*.xlsx)");
    const QString csv = QStringLiteral("CSV file (*.csv)");
    QString selected = excelDefault ? excel : csv;
    const QString def = QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation))
                            .filePath(baseName + (excelDefault ? QStringLiteral(".xlsx") : QStringLiteral(".csv")));
    QString path = QFileDialog::getSaveFileName(parent, title, def, excelDefault ? excel + QStringLiteral(";;") + csv : csv + QStringLiteral(";;") + excel, &selected);
    if (path.isEmpty()) return path;
    if (!path.endsWith(QLatin1String(".xlsx"), Qt::CaseInsensitive) && !path.endsWith(QLatin1String(".csv"), Qt::CaseInsensitive))
        path += selected == csv ? QStringLiteral(".csv") : QStringLiteral(".xlsx");
    return path;
}

QString colorSpan(const QString &text, const QColor &color, bool bold)
{
    return QStringLiteral("<span style='color:%1;%2'>%3</span>").arg(color.name(), bold ? QStringLiteral("font-weight:600;") : QString(), text.toHtmlEscaped());
}

} // namespace ui
