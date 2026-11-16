#pragma once

#include <QWidget>

class QTableWidget;
class SegmentedControl;

class CategoriesPage : public QWidget
{
    Q_OBJECT
public:
    explicit CategoriesPage(QWidget *parent = nullptr);

public slots:
    void refresh();

private:
    QString selectedId() const;
    void addCategory();
    void editCategory(const QString &id);
    void deleteCategory(const QString &id);

    SegmentedControl *m_tab;
    QTableWidget *m_table;
};
