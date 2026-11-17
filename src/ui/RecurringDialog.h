#pragma once

#include "core/Models.h"
#include <QDialog>

class QCheckBox;
class QComboBox;
class QDateEdit;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class SegmentedControl;

/* Create or edit a recurring transaction (rent, internet, salaries...). */
class RecurringDialog : public QDialog
{
    Q_OBJECT
public:
    explicit RecurringDialog(QWidget *parent = nullptr);
    void setNew(TxType type);
    void setRule(const Recurring &r);
    bool deleted() const { return m_deleted; }

private:
    void fillCategories();
    void updatePreview();
    void save();
    void remove();

    Recurring m_rule;
    bool m_editing = false;
    bool m_deleted = false;
    SegmentedControl *m_type;
    QLineEdit *m_amount;
    QComboBox *m_category;
    QLineEdit *m_desc;
    QLineEdit *m_vendor;
    QComboBox *m_payment;
    QComboBox *m_frequency;
    QDateEdit *m_start;
    QComboBox *m_ends;
    QDateEdit *m_end;
    QPlainTextEdit *m_notes;
    QLabel *m_preview;
    QPushButton *m_deleteBtn;
};
