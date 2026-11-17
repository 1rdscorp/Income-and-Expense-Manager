#include "MainWindow.h"

#include "BackupPage.h"
#include "CategoriesPage.h"
#include "DashboardPage.h"
#include "RecurringPage.h"
#include "ReportsPage.h"
#include "SettingsPage.h"
#include "Theme.h"
#include "TransactionDialog.h"
#include "TransactionsPage.h"
#include "Widgets.h"
#include "core/Backup.h"
#include "core/Store.h"
#include "core/Util.h"

#include <QApplication>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QStackedWidget>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

namespace {
struct NavItem {
    const char *title;
    const char *icon;
};
const NavItem kNav[] = {
    {"Dashboard", "dashboard"}, {"Transactions", "list"}, {"Categories", "tag"}, {"Recurring", "repeat"},
    {"Reports", "chart"},       {"Backup & Restore", "database"}, {"Settings", "settings"},
};

QSettings windowSettings()
{
    return QSettings(Store::instance().dataDir() + QStringLiteral("/window.ini"), QSettings::IniFormat);
}

QWidget *scrollWrap(QWidget *page)
{
    auto *sa = new QScrollArea;
    sa->setObjectName(QStringLiteral("pageScroll"));
    sa->setWidgetResizable(true);
    sa->setFrameShape(QFrame::NoFrame);
    sa->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    sa->setWidget(page);
    return sa;
}
} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("Ledgerly — Income & Expense Manager"));
    setWindowIcon(Theme::appIcon());
    setMinimumSize(1040, 680);

    auto *central = new QWidget;
    auto *root = new QHBoxLayout(central);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // Sidebar
    auto *sidebar = new QFrame;
    sidebar->setObjectName(QStringLiteral("sidebar"));
    sidebar->setFixedWidth(236);
    auto *sl = new QVBoxLayout(sidebar);
    sl->setContentsMargins(12, 18, 12, 16);
    sl->setSpacing(6);
    auto *brand = new QHBoxLayout;
    auto *logo = new QLabel;
    logo->setPixmap(Theme::appIcon().pixmap(34, 34));
    auto *bt = new QVBoxLayout;
    bt->setSpacing(0);
    m_brandName = new QLabel;
    m_brandName->setObjectName(QStringLiteral("brandName"));
    auto *brandSub = new QLabel(QStringLiteral("Income & Expenses"));
    brandSub->setObjectName(QStringLiteral("brandSub"));
    bt->addWidget(m_brandName);
    bt->addWidget(brandSub);
    brand->addWidget(logo);
    brand->addLayout(bt, 1);
    sl->addLayout(brand);
    sl->addSpacing(14);
    m_nav = new QListWidget;
    m_nav->setObjectName(QStringLiteral("nav"));
    m_nav->setIconSize(QSize(18, 18));
    m_nav->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_nav->setFocusPolicy(Qt::NoFocus);
    for (const auto &n : kNav) m_nav->addItem(new QListWidgetItem(QString::fromUtf8(n.title)));
    m_nav->setFixedHeight(PageCount * 42);
    sl->addWidget(m_nav);
    sl->addStretch(1);
    m_foot = new QLabel;
    m_foot->setObjectName(QStringLiteral("sidebarFoot"));
    m_foot->setWordWrap(true);
    m_foot->setTextFormat(Qt::RichText);
    sl->addWidget(m_foot);
    root->addWidget(sidebar);

    // Right side
    auto *right = new QWidget;
    right->setObjectName(QStringLiteral("pageRoot"));
    auto *rl = new QVBoxLayout(right);
    rl->setContentsMargins(0, 0, 0, 0);
    rl->setSpacing(0);
    auto *top = new QFrame;
    top->setObjectName(QStringLiteral("topBar"));
    auto *tl = new QHBoxLayout(top);
    tl->setContentsMargins(26, 12, 26, 12);
    m_title = new QLabel;
    m_title->setObjectName(QStringLiteral("pageTitle"));
    tl->addWidget(m_title, 1);
    m_addBtn = ui::button(QStringLiteral("Add transaction"), QStringLiteral("plus"), QStringLiteral("primary"));
    m_addBtn->setToolTip(QStringLiteral("Add a transaction (Ctrl+N)"));
    tl->addWidget(m_addBtn);
    rl->addWidget(top);
    m_stack = new QStackedWidget;
    rl->addWidget(m_stack, 1);
    root->addWidget(right, 1);
    setCentralWidget(central);

    m_dashboard = new DashboardPage;
    m_transactions = new TransactionsPage;
    m_categories = new CategoriesPage;
    m_recurring = new RecurringPage;
    m_reports = new ReportsPage;
    m_backup = new BackupPage;
    m_settings = new SettingsPage;
    m_pages = {m_dashboard, m_transactions, m_categories, m_recurring, m_reports, m_backup, m_settings};
    m_stack->addWidget(scrollWrap(m_dashboard));
    m_stack->addWidget(m_transactions);
    m_stack->addWidget(m_categories);
    m_stack->addWidget(m_recurring);
    m_stack->addWidget(scrollWrap(m_reports));
    m_stack->addWidget(scrollWrap(m_backup));
    m_stack->addWidget(scrollWrap(m_settings));
    m_stale = QVector<bool>(PageCount, true);

    buildMenus();
    refreshIcons();

    connect(m_nav, &QListWidget::currentRowChanged, this, [this](int row) { showPage(row); });
    connect(m_addBtn, &QPushButton::clicked, this, &MainWindow::newTransaction);

    // Page requests
    connect(m_dashboard, &DashboardPage::requestEdit, this, &MainWindow::editTransaction);
    connect(m_dashboard, &DashboardPage::requestAdd, this, &MainWindow::newTransaction);
    connect(m_dashboard, &DashboardPage::requestBackup, this, [this]() { m_backup->backupNow(true); });
    connect(m_dashboard, &DashboardPage::requestPage, this, &MainWindow::showPage);
    connect(m_dashboard, &DashboardPage::monthClicked, this, [this](int y, int m) {
        m_transactions->showMonth(y, m);
        showPage(Transactions);
    });
    connect(m_transactions, &TransactionsPage::requestEdit, this, &MainWindow::editTransaction);
    connect(m_transactions, &TransactionsPage::requestDuplicate, this, &MainWindow::duplicateTransaction);
    connect(m_transactions, &TransactionsPage::requestAdd, this, &MainWindow::newTransaction);

    auto onData = [this]() {
        for (auto &s : m_stale) s = true;
        refreshCurrent();
        refreshChrome();
    };
    connect(&Store::instance(), &Store::dataChanged, this, onData);
    connect(&Store::instance(), &Store::settingsChanged, this, [this, onData]() {
        if (Theme::instance().mode() != Store::instance().settings().theme) Theme::instance().apply(Store::instance().settings().theme);
        onData();
    });
    connect(&Theme::instance(), &Theme::changed, this, [this, onData]() {
        refreshIcons();
        onData();
    });

    // Record recurring payments as they fall due while the app stays open (e.g. overnight).
    auto *recurTimer = new QTimer(this);
    recurTimer->setInterval(5 * 60 * 1000);
    connect(recurTimer, &QTimer::timeout, this, []() { Store::instance().processRecurring(); });
    recurTimer->start();

    QSettings ws = windowSettings();
    if (!restoreGeometry(ws.value(QStringLiteral("geometry")).toByteArray())) resize(1320, 860);

    refreshChrome();
    m_nav->setCurrentRow(Dashboard);
}

void MainWindow::buildMenus()
{
    QMenu *file = menuBar()->addMenu(QStringLiteral("&File"));
    QAction *a = file->addAction(QStringLiteral("&New transaction…"), this, &MainWindow::newTransaction);
    a->setShortcut(QKeySequence::New);
    file->addAction(QStringLiteral("New &recurring transaction…"), this, [this]() {
        showPage(Recurring);
        m_recurring->addRule();
    });
    file->addSeparator();
    a = file->addAction(QStringLiteral("&Back up now…"), this, [this]() { m_backup->backupNow(true); });
    a->setShortcut(QKeySequence(QStringLiteral("Ctrl+B")));
    file->addAction(QStringLiteral("&Restore from backup…"), this, [this]() {
        showPage(Backup);
        m_backup->chooseRestoreFile();
    });
    file->addAction(QStringLiteral("&Export transactions (CSV)…"), this, [this]() { m_backup->exportCsv(); });
    file->addSeparator();
    a = file->addAction(QStringLiteral("E&xit"), this, &QWidget::close);
    a->setShortcut(QKeySequence::Quit);

    QMenu *view = menuBar()->addMenu(QStringLiteral("&View"));
    for (int i = 0; i < PageCount; ++i) {
        QAction *va = view->addAction(QString::fromUtf8(kNav[i].title), this, [this, i]() { showPage(i); });
        va->setShortcut(QKeySequence(QStringLiteral("Ctrl+%1").arg(i + 1)));
    }

    QMenu *help = menuBar()->addMenu(QStringLiteral("&Help"));
    help->addAction(QStringLiteral("Open &data folder"), this, []() {
        QDesktopServices::openUrl(QUrl::fromLocalFile(Store::instance().dataDir()));
    });
    help->addSeparator();
    help->addAction(QStringLiteral("&About Ledgerly"), this, [this]() {
        QMessageBox::about(this, QStringLiteral("About Ledgerly"),
                           QStringLiteral("<h3>Ledgerly %1</h3><p>Income &amp; Expense Manager for small businesses.</p>"
                                          "<p>Your data is stored on this computer in:<br><code>%2</code></p>"
                                          "<p>Built with Qt %3.</p>")
                               .arg(QStringLiteral(LEDGERLY_VERSION), Store::instance().dataDir().toHtmlEscaped(), QString::fromLatin1(qVersion())));
    });
}

void MainWindow::refreshIcons()
{
    const QColor navCol = Theme::c().sidebarText;
    for (int i = 0; i < m_nav->count(); ++i) m_nav->item(i)->setIcon(Theme::icon(QString::fromUtf8(kNav[i].icon), navCol));
    // Recolor every icon button to the current theme
    for (QPushButton *b : findChildren<QPushButton *>()) {
        const QString name = b->property("iconName").toString();
        if (name.isEmpty()) continue;
        const bool light = b->objectName() == QLatin1String("primary") || b->objectName() == QLatin1String("danger");
        b->setIcon(Theme::icon(name, light ? QColor(Qt::white) : QColor()));
    }
}

void MainWindow::refreshChrome()
{
    const Store &s = Store::instance();
    m_brandName->setText(QFontMetrics(m_brandName->font()).elidedText(s.settings().businessName, Qt::ElideRight, 160));
    m_brandName->setToolTip(s.settings().businessName);
    const QString last = s.settings().lastBackupAt;
    const QString dot = s.backupIsStale() ? QStringLiteral("#fab219") : QStringLiteral("#0ca30c");
    m_foot->setText(QStringLiteral("<span style='color:%1'>●</span> %2<br>Data is stored on this computer.")
                        .arg(dot, last.isEmpty() ? QStringLiteral("No backup yet") : QStringLiteral("Last backup %1").arg(util::fmtDateTime(last))));
    setWindowTitle(QStringLiteral("%1 — Ledgerly").arg(s.settings().businessName));
}

void MainWindow::showPage(int page)
{
    if (page < 0 || page >= PageCount) return;
    if (m_nav->currentRow() != page) {
        m_nav->setCurrentRow(page); // re-enters via signal
        return;
    }
    m_stack->setCurrentIndex(page);
    m_title->setText(QString::fromUtf8(kNav[page].title));
    refreshCurrent();
}

int MainWindow::currentPage() const { return m_stack->currentIndex(); }
QWidget *MainWindow::pageWidget(int page) const { return m_pages.value(page); }

void MainWindow::refreshCurrent()
{
    const int i = m_stack->currentIndex();
    if (i < 0 || !m_stale.value(i)) return;
    m_stale[i] = false;
    QMetaObject::invokeMethod(m_pages[i], "refresh");
}

void MainWindow::newTransaction() { openTransactionDialog(QString(), false); }
void MainWindow::editTransaction(const QString &id) { openTransactionDialog(id, false); }
void MainWindow::duplicateTransaction(const QString &id) { openTransactionDialog(id, true); }

void MainWindow::openTransactionDialog(const QString &editId, bool duplicate)
{
    const Transaction *existing = editId.isEmpty() ? nullptr : Store::instance().transaction(editId);
    if (!editId.isEmpty() && !existing) return;
    TxType preset = TxType::Expense;
    Transaction copy;
    if (existing) copy = *existing;
    for (;;) {
        TransactionDialog dlg(this);
        if (existing && !duplicate) dlg.setTransaction(copy);
        else if (existing && duplicate) dlg.setTemplate(copy);
        else dlg.setNewType(preset);
        dlg.exec();
        if (dlg.outcome() != TransactionDialog::SavedAddAnother) break;
        existing = nullptr;
        duplicate = false;
        preset = dlg.savedType();
    }
}

void MainWindow::closeEvent(QCloseEvent *e)
{
    Store &s = Store::instance();
    const ::Settings &st = s.settings();
    if (!st.autoBackupDir.isEmpty() && s.dirtySinceOpen()) {
        QString err;
        if (!backup::writeAutoBackup(s, st.autoBackupDir, st.autoBackupKeep, &err)) {
            const auto r = QMessageBox::warning(this, QStringLiteral("Automatic backup failed"),
                                                QStringLiteral("Ledgerly could not write the automatic backup:\n%1\n\nClose anyway?").arg(err),
                                                QMessageBox::Yes | QMessageBox::No);
            if (r != QMessageBox::Yes) {
                e->ignore();
                return;
            }
        }
    }
    QSettings ws = windowSettings();
    ws.setValue(QStringLiteral("geometry"), saveGeometry());
    e->accept();
}
