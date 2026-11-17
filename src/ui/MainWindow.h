#pragma once

#include <QMainWindow>

class QListWidget;
class QStackedWidget;
class QLabel;
class QPushButton;
class DashboardPage;
class TransactionsPage;
class CategoriesPage;
class RecurringPage;
class ReportsPage;
class BackupPage;
class SettingsPage;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    enum PageId { Dashboard, Transactions, Categories, Recurring, Reports, Backup, Settings, PageCount };

    explicit MainWindow(QWidget *parent = nullptr);

    void showPage(int page);
    int currentPage() const;
    QWidget *pageWidget(int page) const;

public slots:
    void newTransaction();
    void editTransaction(const QString &id);
    void duplicateTransaction(const QString &id);

protected:
    void closeEvent(QCloseEvent *e) override;

private:
    void buildMenus();
    void refreshCurrent();
    void refreshChrome();
    void refreshIcons();
    void openTransactionDialog(const QString &editId, bool duplicate);

    QListWidget *m_nav = nullptr;
    QStackedWidget *m_stack = nullptr;
    QLabel *m_title = nullptr;
    QLabel *m_brandName = nullptr;
    QLabel *m_foot = nullptr;
    QPushButton *m_addBtn = nullptr;
    QVector<QWidget *> m_pages;
    QVector<bool> m_stale;
    DashboardPage *m_dashboard = nullptr;
    TransactionsPage *m_transactions = nullptr;
    CategoriesPage *m_categories = nullptr;
    RecurringPage *m_recurring = nullptr;
    ReportsPage *m_reports = nullptr;
    BackupPage *m_backup = nullptr;
    SettingsPage *m_settings = nullptr;
};
