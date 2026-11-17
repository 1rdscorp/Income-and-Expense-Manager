#pragma once

#include "core/Models.h"
#include <QDialog>

class QCheckBox;
class QComboBox;
class QLineEdit;
class QRadioButton;
class SegmentedControl;

/* Add / edit a bank account or credit card. */
class AccountDialog : public QDialog
{
    Q_OBJECT
public:
    explicit AccountDialog(QWidget *parent = nullptr);
    void setAccount(const Account &a);
    QString savedId() const { return m_savedId; }

private:
    void save();
    Account m_account;
    QString m_savedId;
    QLineEdit *m_name;
    SegmentedControl *m_kind;
    QCheckBox *m_flip;
    QLineEdit *m_folder;
    QComboBox *m_preset;
    class QLabel *m_help;
};

/* "When the description contains … → category / ignore". */
class RuleDialog : public QDialog
{
    Q_OBJECT
public:
    explicit RuleDialog(QWidget *parent = nullptr);
    void setRule(const ImportRule &r);
    void setSuggestion(const QString &pattern, const QString &categoryId);

private:
    void save();
    ImportRule m_rule;
    QLineEdit *m_pattern;
    QRadioButton *m_useCategory;
    QRadioButton *m_ignore;
    QComboBox *m_category;
};

// Fills a combo with all categories (expenses then income), data = category id.
void fillCategoryCombo(QComboBox *combo, const QString &selected = QString(), bool onlyType = false, TxType type = TxType::Expense);
