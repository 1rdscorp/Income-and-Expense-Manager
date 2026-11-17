#include "BankPage.h"

#include "BankDialogs.h"
#include "ImportDialog.h"
#include "Theme.h"
#include "Widgets.h"
#include "core/BankImport.h"
#include "core/Store.h"
#include "core/Util.h"

#include <QDir>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QShortcut>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {
QTableWidget *makeTable(const QStringList &headers)
{
    auto *t = new QTableWidget(0, headers.size());
    t->setHorizontalHeaderLabels(headers);
    t->verticalHeader()->setVisible(false);
    t->setSelectionBehavior(QAbstractItemView::SelectRows);
    t->setSelectionMode(QAbstractItemView::SingleSelection);
    t->setEditTriggers(QAbstractItemView::NoEditTriggers);
    t->setShowGrid(false);
    t->verticalHeader()->setDefaultSectionSize(38);
    t->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    t->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int c = 0; c < headers.size(); ++c) t->horizontalHeaderItem(c)->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    return t;
}
} // namespace

BankPage::BankPage(QWidget *parent)
    : QWidget(parent)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(26, 20, 26, 26);
    root->setSpacing(16);

    auto *bar = new QHBoxLayout;
    auto *intro = ui::muted(QStringLiteral("Import statements from your bank accounts and credit cards. Ledgerly picks categories from your "
                                           "rules and past choices, and skips anything already recorded."));
    intro->setStyleSheet(QStringLiteral("font-size:13px;"));
    bar->addWidget(intro, 1);
    auto *imp = ui::button(QStringLiteral("Import statement…"), QStringLiteral("upload"), QStringLiteral("primary"));
    bar->addWidget(imp);
    root->addLayout(bar);

    // Accounts
    auto *accCard = new Card(QStringLiteral("Accounts & cards"));
    auto *addAcc = ui::button(QStringLiteral("Add"), QStringLiteral("plus"));
    auto *editAcc = ui::button(QStringLiteral("Edit"), QStringLiteral("edit"));
    auto *delAcc = ui::button(QStringLiteral("Delete"), QStringLiteral("trash"), QStringLiteral("dangerText"));
    accCard->header()->addWidget(addAcc);
    accCard->header()->addWidget(editAcc);
    accCard->header()->addWidget(delAcc);
    m_accounts = makeTable({QStringLiteral("Name"), QStringLiteral("Type"), QStringLiteral("Imported"), QStringLiteral("Latest"),
                            QStringLiteral("Automatic import folder")});
    accCard->body()->addWidget(m_accounts);
    root->addWidget(accCard);

    // Rules
    auto *ruleCard = new Card(QStringLiteral("Categorisation rules"), QStringLiteral("applied before your past choices and built-in suggestions"));
    auto *addRuleBtn = ui::button(QStringLiteral("Add rule"), QStringLiteral("plus"));
    auto *editRuleBtn = ui::button(QStringLiteral("Edit"), QStringLiteral("edit"));
    auto *delRuleBtn = ui::button(QStringLiteral("Delete"), QStringLiteral("trash"), QStringLiteral("dangerText"));
    ruleCard->header()->addWidget(addRuleBtn);
    ruleCard->header()->addWidget(editRuleBtn);
    ruleCard->header()->addWidget(delRuleBtn);
    m_rules = makeTable({QStringLiteral("When the description contains"), QStringLiteral("Then")});
    ruleCard->body()->addWidget(m_rules);
    root->addWidget(ruleCard);

    // How it works
    auto *how = new Card(QStringLiteral("How to get your statements"));
    auto *howText = new QLabel(QStringLiteral(
        "<ol style='margin-left:-18px'>"
        "<li>In your online banking, open the account or card and look for <b>Download</b>, <b>Export</b> or <b>Statements</b>.</li>"
        "<li>Choose <b>OFX</b>, <b>QFX</b> (Quicken), <b>QIF</b> or <b>CSV</b> — OFX/QFX work best because they include the bank's own transaction ids.</li>"
        "<li>Click <b>Import statement…</b>, or set an <b>automatic import folder</b> on the account and save downloads there — "
        "Ledgerly imports them within a minute while it's open.</li></ol>"
        "Overlapping statements are fine: lines already imported are skipped, and lines that look like something you entered by hand "
        "(or that a recurring rule recorded) are flagged so nothing is counted twice."));
    howText->setWordWrap(true);
    howText->setTextFormat(Qt::RichText);
    how->body()->addWidget(howText);
    auto *live = ui::muted(QStringLiteral("Live bank connections (automatic downloads straight from the bank) need a third-party banking service "
                                          "such as Plaid, SimpleFIN or GoCardless, which depends on your country and requires an account with them."));
    how->body()->addWidget(live);
    root->addWidget(how);
    root->addStretch(1);

    connect(imp, &QPushButton::clicked, this, &BankPage::importStatement);
    connect(addAcc, &QPushButton::clicked, this, &BankPage::addAccount);
    connect(editAcc, &QPushButton::clicked, this, [this]() { editAccount(selectedAccount()); });
    connect(delAcc, &QPushButton::clicked, this, [this]() { deleteAccount(selectedAccount()); });
    connect(m_accounts, &QTableWidget::cellDoubleClicked, this, [this]() { editAccount(selectedAccount()); });
    connect(addRuleBtn, &QPushButton::clicked, this, &BankPage::addRule);
    connect(editRuleBtn, &QPushButton::clicked, this, [this]() { editRule(selectedRule()); });
    connect(delRuleBtn, &QPushButton::clicked, this, [this]() { deleteRule(selectedRule()); });
    connect(m_rules, &QTableWidget::cellDoubleClicked, this, [this]() { editRule(selectedRule()); });
    connect(new QShortcut(QKeySequence::Delete, m_rules), &QShortcut::activated, this, [this]() { deleteRule(selectedRule()); });
}

QString BankPage::selectedAccount() const
{
    const int r = m_accounts->currentRow();
    return r >= 0 && m_accounts->item(r, 0) ? m_accounts->item(r, 0)->data(Qt::UserRole).toString() : QString();
}

QString BankPage::selectedRule() const
{
    const int r = m_rules->currentRow();
    return r >= 0 && m_rules->item(r, 0) ? m_rules->item(r, 0)->data(Qt::UserRole).toString() : QString();
}

void BankPage::refresh()
{
    const Store &s = Store::instance();
    const auto &th = Theme::c();
    const auto &accounts = s.accounts();
    m_accounts->setRowCount(accounts.size());
    for (int i = 0; i < accounts.size(); ++i) {
        const Account &a = accounts[i];
        auto *name = new QTableWidgetItem(Theme::icon(a.isCard() ? QStringLiteral("card") : QStringLiteral("bank"), th.accent), a.name);
        name->setData(Qt::UserRole, a.id);
        m_accounts->setItem(i, 0, name);
        m_accounts->setItem(i, 1, new QTableWidgetItem(a.isCard() ? QStringLiteral("Credit card") : QStringLiteral("Bank account")));
        QDate latest;
        int n = 0;
        for (const auto &t : s.transactions())
            if (t.accountId == a.id) {
                ++n;
                if (!latest.isValid() || t.date > latest) latest = t.date;
            }
        m_accounts->setItem(i, 2, new QTableWidgetItem(QStringLiteral("%1 transaction%2").arg(n).arg(n == 1 ? QString() : QStringLiteral("s"))));
        m_accounts->setItem(i, 3, new QTableWidgetItem(latest.isValid() ? util::fmtDate(latest) : QStringLiteral("—")));
        const QString folder = bank::watchFolder(s, a.id);
        auto *f = new QTableWidgetItem(folder.isEmpty() ? QStringLiteral("Off") : QDir::toNativeSeparators(folder));
        if (folder.isEmpty()) f->setForeground(th.muted);
        m_accounts->setItem(i, 4, f);
    }
    if (accounts.isEmpty()) {
        m_accounts->setRowCount(1);
        auto *it = new QTableWidgetItem(QStringLiteral("No accounts yet — click Add, or just import a statement."));
        it->setForeground(th.muted);
        it->setFlags(Qt::ItemIsEnabled);
        m_accounts->setItem(0, 0, it);
        for (int c = 1; c < m_accounts->columnCount(); ++c) m_accounts->setItem(0, c, new QTableWidgetItem);
    }
    ui::fitTableHeight(m_accounts);

    const auto &rules = s.importRules();
    m_rules->setRowCount(rules.size());
    for (int i = 0; i < rules.size(); ++i) {
        const ImportRule &r = rules[i];
        auto *p = new QTableWidgetItem(QStringLiteral("“%1”").arg(r.pattern));
        p->setData(Qt::UserRole, r.id);
        m_rules->setItem(i, 0, p);
        const Category c = s.categoryOrUnknown(r.categoryId);
        auto *then = new QTableWidgetItem(r.ignore ? QStringLiteral("Don't import") : c.icon + QStringLiteral("  ") + c.name);
        if (r.ignore) then->setForeground(th.muted);
        m_rules->setItem(i, 1, then);
    }
    if (rules.isEmpty()) {
        m_rules->setRowCount(1);
        auto *it = new QTableWidgetItem(QStringLiteral("No rules yet. Ledgerly also learns from the categories you choose."));
        it->setForeground(th.muted);
        it->setFlags(Qt::ItemIsEnabled);
        m_rules->setItem(0, 0, it);
        m_rules->setItem(0, 1, new QTableWidgetItem);
    }
    ui::fitTableHeight(m_rules);
}

void BankPage::importStatement()
{
    ImportDialog dlg(this);
    const QString acc = selectedAccount();
    if (!acc.isEmpty()) dlg.setAccount(acc);
    if (dlg.exec() == QDialog::Accepted)
        QMessageBox::information(this, QStringLiteral("Statement imported"),
                                 QStringLiteral("%1 transaction%2 imported.").arg(dlg.importedCount()).arg(dlg.importedCount() == 1 ? QString() : QStringLiteral("s")));
}

void BankPage::addAccount()
{
    AccountDialog dlg(this);
    dlg.exec();
}

void BankPage::editAccount(const QString &id)
{
    const Account *a = Store::instance().account(id);
    if (!a) return;
    AccountDialog dlg(this);
    dlg.setAccount(*a);
    if (dlg.exec() == QDialog::Accepted) refresh();
}

void BankPage::deleteAccount(const QString &id)
{
    const Account *a = Store::instance().account(id);
    if (!a) return;
    if (QMessageBox::question(this, QStringLiteral("Delete account"),
                              QStringLiteral("Delete “%1”?\n\nTransactions already imported from it are kept.").arg(a->name)) != QMessageBox::Yes)
        return;
    bank::setWatchFolder(Store::instance(), id, QString());
    QString err;
    if (!Store::instance().deleteAccount(id, &err)) QMessageBox::warning(this, QStringLiteral("Delete account"), err);
}

void BankPage::addRule()
{
    RuleDialog dlg(this);
    dlg.exec();
}

void BankPage::editRule(const QString &id)
{
    for (const auto &r : Store::instance().importRules())
        if (r.id == id) {
            RuleDialog dlg(this);
            dlg.setRule(r);
            dlg.exec();
            return;
        }
}

void BankPage::deleteRule(const QString &id)
{
    if (id.isEmpty()) return;
    if (QMessageBox::question(this, QStringLiteral("Delete rule"), QStringLiteral("Delete this rule?")) == QMessageBox::Yes)
        Store::instance().deleteImportRule(id);
}
