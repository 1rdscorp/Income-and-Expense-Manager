#pragma once

#include "core/Models.h"
#include <QDialog>

class QLineEdit;
class QLabel;
class QWidget;
class QButtonGroup;
class SegmentedControl;

class CategoryDialog : public QDialog
{
    Q_OBJECT
public:
    explicit CategoryDialog(QWidget *parent = nullptr);
    void setNew(TxType type);
    void setCategory(const Category &c);
    TxType savedType() const { return m_cat.type; }

private:
    void save();
    void syncType();

    Category m_cat;
    bool m_editing = false;
    SegmentedControl *m_type;
    QLineEdit *m_name;
    QLineEdit *m_budget;
    QWidget *m_budgetRow;
    QLabel *m_typeHint;
    QButtonGroup *m_icons;
};
