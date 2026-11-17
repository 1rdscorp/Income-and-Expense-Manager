#pragma once

#include "core/Models.h"
#include <QDialog>

class QCheckBox;
class QComboBox;
class QFrame;
class QDateEdit;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class SegmentedControl;

class TransactionDialog : public QDialog
{
    Q_OBJECT
public:
    enum Outcome { Cancelled, Saved, SavedAddAnother, Deleted };

    explicit TransactionDialog(QWidget *parent = nullptr);
    void setNewType(TxType type);
    void setTransaction(const Transaction &t);  // edit
    void setTemplate(const Transaction &t);     // duplicate as new, dated today
    Outcome outcome() const { return m_outcome; }
    TxType savedType() const { return m_type; }

    // Ticks "Repeat this transaction" (used by "Make recurring…" in the transactions list).
    void setRepeatChecked(bool on);

    // Adds files as pending receipts (also used by drag & drop). Returns number added.
    int addFiles(const QStringList &paths);

protected:
    void dragEnterEvent(QDragEnterEvent *e) override;
    void dropEvent(QDropEvent *e) override;

private:
    struct PendingReceipt {
        Receipt r;
        bool isNew = true;
    };
    void fillCategories();
    void syncType();
    void renderReceipts();
    void save(bool another);
    void remove();
    void loadFields(const Transaction &t);
    void syncRepeat();
    void syncSeries();
    void stopRepeating();
    QVector<QDate> missedDates() const;

    Outcome m_outcome = Cancelled;
    TxType m_type = TxType::Expense;
    Transaction m_tx;
    bool m_editing = false;
    QVector<PendingReceipt> m_receipts;
    QStringList m_removed;

    SegmentedControl *m_typeSeg;
    QLineEdit *m_amount;
    QDateEdit *m_date;
    QComboBox *m_category;
    QLineEdit *m_desc;
    QLineEdit *m_vendor;
    QLabel *m_vendorLabel;
    QComboBox *m_payment;
    QPlainTextEdit *m_notes;
    QListWidget *m_receiptList;
    QPushButton *m_viewBtn;
    QPushButton *m_removeBtn;
    QPushButton *m_deleteBtn;
    QPushButton *m_anotherBtn;
    QPushButton *m_saveBtn;
    QWidget *m_repeatRow;
    QCheckBox *m_repeat;
    QComboBox *m_repeatFreq;
    QLabel *m_repeatHint;
    QLabel *m_seriesNote;
    QCheckBox *m_recordMissed;
    QFrame *m_seriesRow;
};
