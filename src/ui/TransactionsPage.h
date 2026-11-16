#pragma once

#include "core/Models.h"
#include <QWidget>

class KpiCard;
class PeriodNav;
class QComboBox;
class QLabel;
class QLineEdit;
class QTreeWidget;
class QTimer;
class SegmentedControl;

class TransactionsPage : public QWidget
{
    Q_OBJECT
public:
    explicit TransactionsPage(QWidget *parent = nullptr);
    void showMonth(int year, int month);

public slots:
    void refresh();

signals:
    void requestEdit(const QString &id);
    void requestDuplicate(const QString &id);
    void requestAdd();

private:
    QVector<Transaction> filtered() const;
    void rebuildCategoryCombo();
    void updateList();
    void deleteSelected();
    QStringList selectedIds() const;
    void showContextMenu(const QPoint &pos);

    SegmentedControl *m_mode;
    PeriodNav *m_period;
    QComboBox *m_type;
    QComboBox *m_category;
    QLineEdit *m_search;
    QVector<KpiCard *> m_summary;
    QTreeWidget *m_tree;
    QLabel *m_emptyHint;
    QTimer *m_searchTimer;
    QDate m_month;
    int m_year = 0;
};
