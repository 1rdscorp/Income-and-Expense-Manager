#include "ReportsPage.h"

#include "BarChart.h"
#include "Theme.h"
#include "Widgets.h"
#include "core/Store.h"
#include "core/Util.h"

#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QUrl>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPageLayout>
#include <QPrintDialog>
#include <QPrinter>
#include <QStandardPaths>
#include <QTableWidget>
#include <QTextDocument>
#include <QVBoxLayout>

using reports::RowKind;

namespace {

QTableWidget *makeTable(const reports::Table &t)
{
    const auto &th = Theme::c();
    auto *table = new QTableWidget(t.rows.size(), t.headers.size());
    table->setHorizontalHeaderLabels(t.headers);
    table->verticalHeader()->setVisible(false);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionMode(QAbstractItemView::NoSelection);
    table->setFocusPolicy(Qt::NoFocus);
    table->setShowGrid(false);
    table->setWordWrap(false);
    table->horizontalHeader()->setHighlightSections(false);
    QFont bold = table->font();
    bold.setWeight(QFont::DemiBold);
    for (int r = 0; r < t.rows.size(); ++r) {
        const RowKind k = t.kinds.value(r, RowKind::Normal);
        for (int c = 0; c < t.headers.size(); ++c) {
            const QString text = (k == RowKind::Section && c > 0) ? QString() : t.rows[r].value(c);
            auto *it = new QTableWidgetItem(text);
            const bool num = t.numericColumns.contains(c);
            it->setTextAlignment((num ? Qt::AlignRight : Qt::AlignLeft) | Qt::AlignVCenter);
            if (k != RowKind::Normal) it->setFont(bold);
            if (k == RowKind::Section) it->setBackground(th.surface3);
            if (k == RowKind::Net) it->setBackground(th.accentSoft);
            if (k == RowKind::Total) it->setBackground(th.surface2);
            if (text.startsWith(QChar(0x2212))) it->setForeground(th.badText);
            else if (text == QStringLiteral("–") || text == QStringLiteral("—")) it->setForeground(th.muted);
            table->setItem(r, c, it);
        }
    }
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    if (t.headers.size() <= 9) table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    else table->horizontalHeader()->setStretchLastSection(false);
    for (int c = 0; c < t.headers.size(); ++c)
        if (auto *h = table->horizontalHeaderItem(c)) h->setTextAlignment((t.numericColumns.contains(c) ? Qt::AlignRight : Qt::AlignLeft) | Qt::AlignVCenter);
    table->verticalHeader()->setDefaultSectionSize(30);
    table->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    return table;
}

QWidget *tableCard(const reports::Table &t)
{
    auto *card = new Card(t.title, t.rows.isEmpty() ? QString() : t.subtitle);
    if (t.rows.isEmpty()) {
        card->body()->addWidget(ui::muted(t.emptyText));
        card->body()->addStretch(1);
        return card;
    }
    auto *table = makeTable(t);
    card->body()->addWidget(table);
    card->body()->addStretch(1);
    // size after layout so the horizontal scrollbar need is known
    QMetaObject::invokeMethod(table, [table]() { ui::fitTableHeight(table); }, Qt::QueuedConnection);
    ui::fitTableHeight(table);
    return card;
}

} // namespace

ReportsPage::ReportsPage(QWidget *parent)
    : QWidget(parent)
{
    const QDate today = QDate::currentDate();
    m_month = QDate(today.year(), today.month(), 1);
    m_year = today.year();

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(26, 20, 26, 26);
    root->setSpacing(16);
    auto *bar = new QHBoxLayout;
    bar->setSpacing(10);
    m_tab = new SegmentedControl({{QStringLiteral("monthly"), QStringLiteral("Monthly")}, {QStringLiteral("annual"), QStringLiteral("Annual")}, {QStringLiteral("yoy"), QStringLiteral("Year over year")}});
    m_period = new PeriodNav;
    auto *excel = ui::button(QStringLiteral("Export to Excel"), QStringLiteral("download"), QStringLiteral("primary"));
    excel->setToolTip(QStringLiteral("Save this report as an Excel workbook (.xlsx)"));
    auto *csv = ui::button(QStringLiteral("CSV"), QStringLiteral("download"));
    csv->setToolTip(QStringLiteral("Save this report as a CSV file"));
    auto *pdf = ui::button(QStringLiteral("Save as PDF"), QStringLiteral("pdf"));
    auto *prn = ui::button(QStringLiteral("Print…"), QStringLiteral("print"));
    bar->addWidget(m_tab);
    bar->addWidget(m_period);
    bar->addStretch(1);
    bar->addWidget(excel);
    bar->addWidget(csv);
    bar->addWidget(pdf);
    bar->addWidget(prn);
    root->addLayout(bar);
    m_content = new QVBoxLayout;
    m_content->setSpacing(16);
    root->addLayout(m_content);
    root->addStretch(1);

    connect(m_tab, &SegmentedControl::changed, this, &ReportsPage::refresh);
    connect(m_period, &PeriodNav::stepped, this, [this](int d) {
        if (m_tab->value() == QLatin1String("monthly")) m_month = m_month.addMonths(d);
        else m_year += d;
        refresh();
    });
    connect(excel, &QPushButton::clicked, this, &ReportsPage::exportExcel);
    connect(csv, &QPushButton::clicked, this, &ReportsPage::exportCsv);
    connect(pdf, &QPushButton::clicked, this, &ReportsPage::exportPdf);
    connect(prn, &QPushButton::clicked, this, &ReportsPage::print);
}

void ReportsPage::refresh()
{
    const Store &s = Store::instance();
    const QString tab = m_tab->value();
    if (tab == QLatin1String("monthly")) m_report = reports::monthly(s, m_month.year(), m_month.month());
    else if (tab == QLatin1String("annual")) m_report = reports::annual(s, m_year);
    else m_report = reports::yearOverYear(s);
    m_period->setVisible(tab != QLatin1String("yoy"));
    m_period->setLabel(m_report.period);

    ui::clearLayout(m_content);
    const auto &th = Theme::c();

    if (!m_report.stats.isEmpty()) {
        auto *row = new QHBoxLayout;
        row->setSpacing(12);
        for (const auto &st : m_report.stats) {
            auto *k = new KpiCard;
            const QColor sw = st.swatch == QLatin1String("income") ? th.income : st.swatch == QLatin1String("expense") ? th.expense : QColor();
            k->set(st.label, st.value, st.meta.toHtmlEscaped(), sw, st.negative);
            row->addWidget(k);
        }
        m_content->addLayout(row);
    }

    if (m_report.chart.valid) {
        auto *card = new Card(m_report.chart.title);
        auto *legend = new QLabel(QStringLiteral("<span style='color:%1'>■</span> Income &nbsp;&nbsp;<span style='color:%2'>■</span> Expenses").arg(th.income.name(), th.expense.name()));
        card->header()->addWidget(legend);
        auto *chart = new BarChart;
        chart->setData(m_report.chart.labels, m_report.chart.fullLabels, m_report.chart.income, m_report.chart.expense);
        card->body()->addWidget(chart);
        card->body()->addWidget(ui::muted(m_report.kind == QLatin1String("annual") ? QStringLiteral("Click a month to open its monthly report.")
                                                                                    : QStringLiteral("Click a year to open its annual report.")));
        const QString kind = m_report.kind;
        const int year = m_year;
        connect(chart, &BarChart::columnClicked, this, [this, kind, year](int i) {
            if (kind == QLatin1String("annual")) {
                m_month = QDate(year, i + 1, 1);
                m_tab->setValue(QStringLiteral("monthly"));
            } else {
                m_year = m_report.chart.labels.value(i).toInt();
                m_tab->setValue(QStringLiteral("annual"));
            }
            QMetaObject::invokeMethod(this, &ReportsPage::refresh, Qt::QueuedConnection);
        });
        m_content->addWidget(card);
    }

    for (int i = 0; i < m_report.tables.size(); ++i) {
        const auto &t = m_report.tables[i];
        if (t.half && i + 1 < m_report.tables.size() && m_report.tables[i + 1].half) {
            auto *row = new QHBoxLayout;
            row->setSpacing(16);
            row->addWidget(tableCard(t), 1);
            row->addWidget(tableCard(m_report.tables[i + 1]), 1);
            m_content->addLayout(row);
            ++i;
        } else {
            m_content->addWidget(tableCard(t));
        }
    }
}

QString ReportsPage::baseFileName() const
{
    return QStringLiteral("%1-%2-report-%3").arg(util::slug(Store::instance().settings().businessName), m_report.kind, util::slug(m_report.period));
}

void ReportsPage::exportExcel()
{
    const QString path = ui::askExportPath(this, QStringLiteral("Export report to Excel"), baseFileName());
    if (path.isEmpty()) return;
    QString err;
    bool ok;
    if (path.endsWith(QLatin1String(".csv"), Qt::CaseInsensitive)) {
        QFile f(path);
        ok = f.open(QIODevice::WriteOnly) && f.write(util::toCsv(m_report.csv).toUtf8()) >= 0;
        err = f.errorString();
    } else {
        ok = reports::saveXlsx(m_report, Store::instance().settings().businessName, path, &err);
    }
    if (!ok) {
        QMessageBox::warning(this, QStringLiteral("Export failed"), err);
        return;
    }
    const auto r = QMessageBox::information(this, QStringLiteral("Report exported"),
                                            QStringLiteral("Saved to:\n%1").arg(QDir::toNativeSeparators(path)),
                                            QMessageBox::Open | QMessageBox::Ok, QMessageBox::Ok);
    if (r == QMessageBox::Open) QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

void ReportsPage::exportCsv()
{
    const QString name = QStringLiteral("%1-%2-report-%3.csv").arg(util::slug(Store::instance().settings().businessName), m_report.kind, util::slug(m_report.period));
    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Export report as CSV"),
                                                      QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + QLatin1Char('/') + name,
                                                      QStringLiteral("CSV files (*.csv)"));
    if (path.isEmpty()) return;
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(util::toCsv(m_report.csv).toUtf8()) < 0)
        QMessageBox::warning(this, QStringLiteral("Export failed"), f.errorString());
}

void ReportsPage::buildDocument(QTextDocument *doc)
{
    doc->setHtml(reports::toHtml(m_report, Store::instance().settings().businessName));
}

static void setupPrinter(QPrinter &printer, const reports::Report &r)
{
    printer.setPageSize(QPageSize(QPageSize::A4));
    printer.setPageOrientation(r.kind == QLatin1String("annual") || r.kind == QLatin1String("yoy") ? QPageLayout::Landscape : QPageLayout::Portrait);
    printer.setPageMargins(QMarginsF(12, 12, 12, 12), QPageLayout::Millimeter);
}

void ReportsPage::exportPdf()
{
    const QString name = QStringLiteral("%1-%2-report-%3.pdf").arg(util::slug(Store::instance().settings().businessName), m_report.kind, util::slug(m_report.period));
    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Save report as PDF"),
                                                      QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + QLatin1Char('/') + name,
                                                      QStringLiteral("PDF files (*.pdf)"));
    if (path.isEmpty()) return;
    QPrinter printer(QPrinter::HighResolution);
    printer.setOutputFormat(QPrinter::PdfFormat);
    printer.setOutputFileName(path);
    setupPrinter(printer, m_report);
    QTextDocument doc;
    buildDocument(&doc);
    doc.print(&printer);
}

void ReportsPage::print()
{
    QPrinter printer(QPrinter::HighResolution);
    setupPrinter(printer, m_report);
    QPrintDialog dlg(&printer, this);
    dlg.setWindowTitle(QStringLiteral("Print report"));
    if (dlg.exec() != QDialog::Accepted) return;
    QTextDocument doc;
    buildDocument(&doc);
    doc.print(&printer);
}
