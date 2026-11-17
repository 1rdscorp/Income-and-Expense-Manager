#pragma once

#include "core/Reports.h"
#include <QDate>
#include <QWidget>

class PeriodNav;
class QVBoxLayout;
class SegmentedControl;
class QTextDocument;

class ReportsPage : public QWidget
{
    Q_OBJECT
public:
    explicit ReportsPage(QWidget *parent = nullptr);

public slots:
    void refresh();
    void exportExcel();

private:
    void exportCsv();
    QString baseFileName() const;
    void exportPdf();
    void print();
    void buildDocument(QTextDocument *doc);

    SegmentedControl *m_tab;
    PeriodNav *m_period;
    QVBoxLayout *m_content;
    QDate m_month;
    int m_year = 0;
    reports::Report m_report;
};
