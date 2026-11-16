#include "SettingsPage.h"

#include "Theme.h"
#include "Widgets.h"
#include "core/Defaults.h"
#include "core/Store.h"
#include "core/Util.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QGridLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>
#include <functional>
#include <algorithm>

namespace {

/* A settings row: bold title + muted description on the left, control on the right. */
void addRow(QGridLayout *g, int row, const QString &title, const QString &desc, QWidget *control)
{
    auto *box = new QWidget;
    auto *l = new QVBoxLayout(box);
    l->setContentsMargins(0, 6, 0, 6);
    l->setSpacing(2);
    auto *t = new QLabel(title);
    t->setStyleSheet(QStringLiteral("font-weight:600;"));
    l->addWidget(t);
    if (!desc.isEmpty()) l->addWidget(ui::muted(desc));
    g->addWidget(box, row, 0);
    if (control) g->addWidget(control, row, 1, Qt::AlignRight | Qt::AlignVCenter);
}

QGridLayout *grid(Card *c)
{
    auto *g = new QGridLayout;
    g->setColumnStretch(0, 1);
    g->setHorizontalSpacing(24);
    c->body()->addLayout(g);
    return g;
}

} // namespace

SettingsPage::SettingsPage(QWidget *parent)
    : QWidget(parent)
{
    auto *outer = new QHBoxLayout(this);
    outer->setContentsMargins(26, 20, 26, 26);
    auto *col = new QWidget;
    col->setMaximumWidth(900);
    auto *root = new QVBoxLayout(col);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(16);
    outer->addWidget(col, 1);
    outer->addStretch(0);

    // Business
    auto *biz = new Card(QStringLiteral("Business"));
    auto *g = grid(biz);
    m_name = new QLineEdit;
    m_name->setMaxLength(80);
    m_name->setMinimumWidth(260);
    addRow(g, 0, QStringLiteral("Business name"), QStringLiteral("Shown in the menu, reports and backup file names."), m_name);
    m_currency = new QComboBox;
    m_currency->setMinimumWidth(260);
    for (const auto &c : defaults::currencies()) m_currency->addItem(QStringLiteral("%1 — %2 (%3)").arg(c.code, c.name, c.symbol.trimmed()), c.code);
    addRow(g, 1, QStringLiteral("Currency"), QStringLiteral("Used to format all amounts. Changing it does not convert existing values."), m_currency);
    root->addWidget(biz);

    // Preferences
    auto *pref = new Card(QStringLiteral("Preferences"));
    g = grid(pref);
    m_theme = new SegmentedControl({{QStringLiteral("system"), QStringLiteral("System")}, {QStringLiteral("light"), QStringLiteral("Light")}, {QStringLiteral("dark"), QStringLiteral("Dark")}});
    addRow(g, 0, QStringLiteral("Appearance"), QStringLiteral("Follow Windows, or always use light or dark."), m_theme);
    m_compress = new QCheckBox(QStringLiteral("Enabled"));
    addRow(g, 1, QStringLiteral("Compress photo receipts"), QStringLiteral("Shrinks large phone photos (max 2000px JPEG) to save space and keep backups small."), m_compress);
    m_reminder = new QComboBox;
    m_reminder->addItem(QStringLiteral("Off"), 0);
    m_reminder->addItem(QStringLiteral("Daily"), 1);
    m_reminder->addItem(QStringLiteral("Weekly"), 7);
    m_reminder->addItem(QStringLiteral("Monthly"), 30);
    addRow(g, 2, QStringLiteral("Backup reminder"), QStringLiteral("Show a reminder when your last backup is older than this."), m_reminder);
    root->addWidget(pref);

    // Storage
    auto *storage = new Card(QStringLiteral("Storage"));
    g = grid(storage);
    auto *openDir = ui::button(QStringLiteral("Open folder"), QStringLiteral("folder"));
    m_dataDir = ui::muted(QString());
    m_dataDir->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto *dirBox = new QWidget;
    auto *dl = new QVBoxLayout(dirBox);
    dl->setContentsMargins(0, 6, 0, 6);
    dl->setSpacing(2);
    auto *dt = new QLabel(QStringLiteral("Data folder"));
    dt->setStyleSheet(QStringLiteral("font-weight:600;"));
    dl->addWidget(dt);
    dl->addWidget(m_dataDir);
    g->addWidget(dirBox, 0, 0);
    g->addWidget(openDir, 0, 1, Qt::AlignRight | Qt::AlignVCenter);
    m_usage = ui::muted(QString());
    auto *usageBox = new QWidget;
    auto *ul = new QVBoxLayout(usageBox);
    ul->setContentsMargins(0, 6, 0, 6);
    auto *ut = new QLabel(QStringLiteral("Space used"));
    ut->setStyleSheet(QStringLiteral("font-weight:600;"));
    ul->addWidget(ut);
    ul->addWidget(m_usage);
    g->addWidget(usageBox, 1, 0);
    root->addWidget(storage);
    connect(openDir, &QPushButton::clicked, this, []() { QDesktopServices::openUrl(QUrl::fromLocalFile(Store::instance().dataDir())); });

    // Data
    auto *data = new Card(QStringLiteral("Categories & data"));
    g = grid(data);
    auto *restoreCats = ui::button(QStringLiteral("Restore defaults"));
    addRow(g, 0, QStringLiteral("Restore standard categories"),
           QStringLiteral("Adds back any of the %1 default business categories you've removed. Your own categories are kept.").arg(defaults::categories().size()), restoreCats);
    m_sample = ui::button(QStringLiteral("Load sample data"));
    addRow(g, 1, QStringLiteral("Sample data"), QStringLiteral("Fill the app with six months of example transactions to explore the reports. Only available when there are no transactions."), m_sample);
    root->addWidget(data);

    // Danger zone
    auto *danger = new Card(QStringLiteral("Danger zone"));
    g = grid(danger);
    auto *erase = ui::button(QStringLiteral("Erase everything"), QStringLiteral("trash"), QStringLiteral("danger"));
    addRow(g, 0, QStringLiteral("Erase all data"),
           QStringLiteral("Deletes every transaction, receipt and category on this computer and resets settings. Make a backup first."), erase);
    root->addWidget(danger);

    root->addWidget(ui::muted(QStringLiteral("Ledgerly %1 · Your data never leaves this computer unless you export it. Press Ctrl+N anywhere to add a transaction.")
                                  .arg(QStringLiteral(LEDGERLY_VERSION))));
    root->addStretch(1);

    auto save = [](const std::function<void(Settings &)> &fn) {
        Settings s = Store::instance().settings();
        fn(s);
        Store::instance().saveSettings(s);
    };
    connect(m_name, &QLineEdit::editingFinished, this, [this, save]() {
        const QString n = m_name->text().simplified();
        if (n == Store::instance().settings().businessName) return;
        save([n](Settings &s) { s.businessName = n.isEmpty() ? QStringLiteral("My Business") : n; });
    });
    connect(m_currency, &QComboBox::currentIndexChanged, this, [this, save]() {
        const QString c = m_currency->currentData().toString();
        if (c != Store::instance().settings().currency) save([c](Settings &s) { s.currency = c; });
    });
    connect(m_theme, &SegmentedControl::changed, this, [save](const QString &v) { save([v](Settings &s) { s.theme = v; }); });
    connect(m_compress, &QCheckBox::toggled, this, [save](bool on) { save([on](Settings &s) { s.compressReceipts = on; }); });
    connect(m_reminder, &QComboBox::currentIndexChanged, this, [this, save]() {
        const int d = m_reminder->currentData().toInt();
        save([d](Settings &s) { s.backupReminderDays = d; });
    });
    connect(restoreCats, &QPushButton::clicked, this, [this]() {
        const int n = Store::instance().seedDefaultCategories(false);
        QMessageBox::information(this, QStringLiteral("Categories"),
                                 n ? QStringLiteral("%1 categor%2 restored.").arg(n).arg(n == 1 ? QStringLiteral("y") : QStringLiteral("ies"))
                                   : QStringLiteral("All standard categories are already present."));
    });
    connect(m_sample, &QPushButton::clicked, this, [this]() {
        if (QMessageBox::question(this, QStringLiteral("Load sample data"),
                                  QStringLiteral("Add about six months of example transactions?\nYou can remove them later with Erase all data.")) == QMessageBox::Yes)
            Store::instance().loadSampleData();
    });
    connect(erase, &QPushButton::clicked, this, [this]() {
        bool ok = false;
        const QString typed = QInputDialog::getText(this, QStringLiteral("Erase all data"),
                                                    QStringLiteral("This permanently deletes %1 transactions, all receipts and all categories.\n"
                                                                   "It cannot be undone.\n\nType DELETE to confirm:").arg(Store::instance().transactions().size()),
                                                    QLineEdit::Normal, QString(), &ok);
        if (!ok) return;
        if (typed.trimmed() != QLatin1String("DELETE")) {
            QMessageBox::information(this, QStringLiteral("Erase all data"), QStringLiteral("Nothing was erased."));
            return;
        }
        QString err;
        if (!Store::instance().eraseAll(&err)) QMessageBox::warning(this, QStringLiteral("Erase failed"), err);
        else QMessageBox::information(this, QStringLiteral("Erase all data"), QStringLiteral("All data was erased."));
    });
}

void SettingsPage::refresh()
{
    const Store &s = Store::instance();
    const Settings &st = s.settings();
    QSignalBlocker b1(m_name), b2(m_currency), b3(m_compress), b4(m_reminder);
    if (!m_name->hasFocus()) m_name->setText(st.businessName);
    m_currency->setCurrentIndex(std::max(0, m_currency->findData(st.currency)));
    m_theme->setValue(st.theme);
    m_compress->setChecked(st.compressReceipts);
    m_reminder->setCurrentIndex(std::max(0, m_reminder->findData(st.backupReminderDays)));
    m_dataDir->setText(QDir::toNativeSeparators(s.dataDir()));
    m_usage->setText(QStringLiteral("Database %1 · receipts %2 in %3 file(s)")
                         .arg(util::fmtBytes(QFileInfo(s.databasePath()).size()), util::fmtBytes(s.receiptBytes()))
                         .arg(s.totalReceipts()));
    m_sample->setEnabled(s.transactions().isEmpty());
}
