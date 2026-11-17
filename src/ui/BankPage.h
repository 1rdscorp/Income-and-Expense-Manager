#pragma once

#include <QWidget>

class QTableWidget;

class BankPage : public QWidget
{
    Q_OBJECT
public:
    explicit BankPage(QWidget *parent = nullptr);

public slots:
    void refresh();
    void importStatement();

private:
    QString selectedAccount() const;
    QString selectedRule() const;
    void addAccount();
    void editAccount(const QString &id);
    void deleteAccount(const QString &id);
    void addRule();
    void editRule(const QString &id);
    void deleteRule(const QString &id);

    QTableWidget *m_accounts;
    QTableWidget *m_rules;
};
