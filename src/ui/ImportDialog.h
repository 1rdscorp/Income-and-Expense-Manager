#pragma once

#include "core/BankImport.h"
#include <QDialog>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;
class QWidget;

/* Import a bank / credit-card statement: choose account and file, check the
 * columns (CSV), review categories and duplicates, then import. */
class ImportDialog : public QDialog
{
    Q_OBJECT
public:
    explicit ImportDialog(QWidget *parent = nullptr);
    void setAccount(const QString &accountId);
    bool loadFile(const QString &path);
    int importedCount() const { return m_imported; }
    const QVector<bank::Proposed> &items() const { return m_items; }
    void doImport();

private:
    void chooseFile();
    void fillAccounts(const QString &select);
    void fillMappingCombos();
    bank::CsvMapping mappingFromUi() const;
    void reparse(bool fromUi);
    void prepareItems();
    void rebuildTable();
    void updateSummary();
    void makeRuleFromSelection();
    Account currentAccount() const;

    QString m_path;
    QByteArray m_data;
    bank::ParseResult m_parse;
    QVector<bank::Proposed> m_items;
    int m_imported = 0;
    bool m_updating = false;

    QComboBox *m_account;
    QLineEdit *m_file;
    QLabel *m_info;
    QWidget *m_csvBox;
    QComboBox *m_cDate, *m_cDesc, *m_cAmount, *m_cDebit, *m_cCredit, *m_cFormat;
    QCheckBox *m_flip;
    QTableWidget *m_table;
    QLabel *m_summary;
    QPushButton *m_importBtn;
    QPushButton *m_ruleBtn;
};
