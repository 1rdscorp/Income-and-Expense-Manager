#pragma once

#include <QWidget>

class Banner;
class BarChart;
class Card;
class HBarList;
class KpiCard;
class QTreeWidget;
class QWidget;
class QLabel;
class QFrame;

class DashboardPage : public QWidget
{
    Q_OBJECT
public:
    explicit DashboardPage(QWidget *parent = nullptr);

public slots:
    void refresh();

signals:
    void requestEdit(const QString &id);
    void requestAdd();
    void requestBackup();
    void requestPage(int page);
    void monthClicked(int year, int month);

private:
    Banner *m_banner;
    QWidget *m_empty;
    QWidget *m_content;
    QVector<KpiCard *> m_kpis;
    Card *m_chartCard;
    BarChart *m_chart;
    Card *m_topCard;
    HBarList *m_top;
    QTreeWidget *m_recent;
    HBarList *m_week;
    QTreeWidget *m_upcoming;
    QLabel *m_upcomingEmpty;
    QFrame *m_autoBanner;
    QLabel *m_autoText;
    int m_chartYear = 0;
};
