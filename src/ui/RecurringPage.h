#pragma once

#include <QWidget>

class KpiCard;
class QLabel;
class QPushButton;
class QTableWidget;
class QWidget;

class RecurringPage : public QWidget
{
    Q_OBJECT
public:
    explicit RecurringPage(QWidget *parent = nullptr);

public slots:
    void refresh();
    void addRule();

private:
    QString selectedId() const;
    void editRule(const QString &id);
    void togglePause(const QString &id);
    void deleteRule(const QString &id);
    void updateButtons();

    QVector<KpiCard *> m_kpis;
    QTableWidget *m_table;
    QWidget *m_empty;
    QPushButton *m_edit;
    QPushButton *m_pause;
    QPushButton *m_delete;
};
