#include "BackupPage.h"

#include "Theme.h"
#include "Widgets.h"
#include "core/Backup.h"
#include "core/Store.h"
#include "core/Util.h"

#include <QApplication>
#include <QCheckBox>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QRadioButton>
#include <QSpinBox>
#include <QStandardPaths>
#include <QUrl>
#include <QVBoxLayout>

namespace {
QString docsDir() { return QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation); }
}

BackupPage::BackupPage(QWidget *parent)
    : QWidget(parent)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(26, 20, 26, 26);
    root->setSpacing(16);

    m_banner = new Banner;
    m_banner->button()->setText(QStringLiteral("Back up now"));
    m_banner->button()->setProperty("iconName", QStringLiteral("download"));
    connect(m_banner->button(), &QPushButton::clicked, this, [this]() { backupNow(true); });
    root->addWidget(m_banner);

    // Create
    auto *create = new Card(QStringLiteral("Create a backup"));
    m_last = new QLabel;
    m_last->setObjectName(QStringLiteral("cardSub"));
    create->header()->addWidget(m_last);
    m_stats = new QLabel;
    m_stats->setTextFormat(Qt::RichText);
    create->body()->addWidget(m_stats);
    create->body()->addWidget(ui::muted(QStringLiteral("Saves a single .json file with your settings, categories, transactions and receipt files. "
                                                       "Keep it on a USB drive or cloud folder (OneDrive, Google Drive, Dropbox). "
                                                       "Backups are compatible with the Ledgerly web app.")));
    auto *row = new QHBoxLayout;
    auto *full = ui::button(QStringLiteral("Back up now"), QStringLiteral("download"), QStringLiteral("primary"));
    auto *lite = ui::button(QStringLiteral("Backup without receipts"), QStringLiteral("download"));
    auto *csv = ui::button(QStringLiteral("Export all transactions (Excel / CSV)"), QStringLiteral("download"));
    row->addWidget(full);
    row->addWidget(lite);
    row->addWidget(csv);
    row->addStretch(1);
    create->body()->addLayout(row);
    root->addWidget(create);
    connect(full, &QPushButton::clicked, this, [this]() { backupNow(true); });
    connect(lite, &QPushButton::clicked, this, [this]() { backupNow(false); });
    connect(csv, &QPushButton::clicked, this, &BackupPage::exportCsv);

    // Automatic
    auto *autoCard = new Card(QStringLiteral("Automatic backups"));
    m_autoOn = new QCheckBox(QStringLiteral("Back up automatically when Ledgerly closes (only if something changed)"));
    autoCard->body()->addWidget(m_autoOn);
    auto *dirRow = new QHBoxLayout;
    dirRow->addWidget(new QLabel(QStringLiteral("Folder:")));
    m_autoDir = new QLineEdit;
    m_autoDir->setReadOnly(true);
    m_autoDir->setPlaceholderText(QStringLiteral("Choose a folder — ideally one that syncs to the cloud"));
    m_autoBrowse = ui::button(QStringLiteral("Choose…"), QStringLiteral("folder"));
    m_autoOpen = ui::button(QStringLiteral("Open"), QStringLiteral("external"));
    dirRow->addWidget(m_autoDir, 1);
    dirRow->addWidget(m_autoBrowse);
    dirRow->addWidget(m_autoOpen);
    autoCard->body()->addLayout(dirRow);
    auto *keepRow = new QHBoxLayout;
    keepRow->addWidget(new QLabel(QStringLiteral("Keep the most recent")));
    m_autoKeep = new QSpinBox;
    m_autoKeep->setRange(1, 100);
    keepRow->addWidget(m_autoKeep);
    keepRow->addWidget(new QLabel(QStringLiteral("backups (older ones are deleted)")));
    keepRow->addStretch(1);
    autoCard->body()->addLayout(keepRow);
    root->addWidget(autoCard);
    connect(m_autoOn, &QCheckBox::toggled, this, [this](bool on) {
        if (on && m_autoDir->text().isEmpty()) {
            const QString d = QFileDialog::getExistingDirectory(this, QStringLiteral("Choose a folder for automatic backups"), docsDir());
            if (d.isEmpty()) {
                QSignalBlocker b(m_autoOn);
                m_autoOn->setChecked(false);
                return;
            }
            m_autoDir->setText(QDir::toNativeSeparators(d));
        }
        saveAutoSettings();
    });
    connect(m_autoBrowse, &QPushButton::clicked, this, [this]() {
        const QString d = QFileDialog::getExistingDirectory(this, QStringLiteral("Choose a folder for automatic backups"),
                                                            m_autoDir->text().isEmpty() ? docsDir() : m_autoDir->text());
        if (d.isEmpty()) return;
        m_autoDir->setText(QDir::toNativeSeparators(d));
        {
            QSignalBlocker b(m_autoOn);
            m_autoOn->setChecked(true);
        }
        saveAutoSettings();
    });
    connect(m_autoOpen, &QPushButton::clicked, this, [this]() {
        if (!m_autoDir->text().isEmpty()) QDesktopServices::openUrl(QUrl::fromLocalFile(QDir::fromNativeSeparators(m_autoDir->text())));
    });
    connect(m_autoKeep, &QSpinBox::editingFinished, this, &BackupPage::saveAutoSettings);

    // Restore
    auto *restoreCard = new Card(QStringLiteral("Restore from a backup"));
    auto *choose = ui::button(QStringLiteral("Choose backup file…"), QStringLiteral("upload"));
    auto *chooseRow = new QHBoxLayout;
    chooseRow->addWidget(choose);
    chooseRow->addWidget(ui::muted(QStringLiteral("Accepts backups from this app or from the Ledgerly web app.")), 1);
    restoreCard->body()->addLayout(chooseRow);
    m_restoreInfo = new QLabel;
    m_restoreInfo->setTextFormat(Qt::RichText);
    m_restoreInfo->setWordWrap(true);
    m_restoreInfo->hide();
    restoreCard->body()->addWidget(m_restoreInfo);
    m_restoreOptions = new QWidget;
    auto *ro = new QVBoxLayout(m_restoreOptions);
    ro->setContentsMargins(0, 4, 0, 0);
    m_replace = new QRadioButton(QStringLiteral("Replace all data — erase everything here and load the backup exactly"));
    m_merge = new QRadioButton(QStringLiteral("Merge — keep current data and add anything from the backup that isn't already here"));
    m_replace->setChecked(true);
    auto *go = ui::button(QStringLiteral("Restore"), QStringLiteral("upload"), QStringLiteral("primary"));
    ro->addWidget(m_replace);
    ro->addWidget(m_merge);
    auto *goRow = new QHBoxLayout;
    goRow->addWidget(go);
    goRow->addStretch(1);
    ro->addLayout(goRow);
    m_restoreOptions->hide();
    restoreCard->body()->addWidget(m_restoreOptions);
    root->addWidget(restoreCard);
    root->addStretch(1);
    connect(choose, &QPushButton::clicked, this, &BackupPage::chooseRestoreFile);
    connect(go, &QPushButton::clicked, this, &BackupPage::restore);
}

void BackupPage::refresh()
{
    const Store &s = Store::instance();
    const QString last = s.settings().lastBackupAt;
    m_banner->setVisible(s.backupIsStale());
    m_banner->setText(QStringLiteral("%1 Your data is stored only on this computer — if the disk fails or the computer is lost, so is your data.")
                          .arg(last.isEmpty() ? QStringLiteral("<b>You haven't made a backup yet.</b>")
                                              : QStringLiteral("Your last backup was on <b>%1</b>.").arg(util::fmtDateTime(last))));
    m_last->setText(last.isEmpty() ? QStringLiteral("No backups yet") : QStringLiteral("Last backup: %1").arg(util::fmtDateTime(last)));
    auto stat = [](const QString &v, const QString &l) {
        return QStringLiteral("<td style='padding-right:36px'><span style='font-size:18px;font-weight:700'>%1</span><br><span style='color:%3'>%2</span></td>")
            .arg(v.toHtmlEscaped(), l, Theme::c().muted.name());
    };
    m_stats->setText(QStringLiteral("<table><tr>%1%2%3%4</tr></table>")
                         .arg(stat(QString::number(s.categories().size()), QStringLiteral("Categories")),
                              stat(QString::number(s.transactions().size()), QStringLiteral("Transactions")),
                              stat(QString::number(s.totalReceipts()), QStringLiteral("Receipts")),
                              stat(util::fmtBytes(s.receiptBytes()), QStringLiteral("Receipt storage"))));
    const Settings &st = s.settings();
    QSignalBlocker b1(m_autoOn), b2(m_autoKeep);
    m_autoOn->setChecked(!st.autoBackupDir.isEmpty());
    m_autoDir->setText(QDir::toNativeSeparators(st.autoBackupDir));
    m_autoKeep->setValue(st.autoBackupKeep);
    m_autoOpen->setEnabled(!st.autoBackupDir.isEmpty());
}

void BackupPage::saveAutoSettings()
{
    Settings st = Store::instance().settings();
    st.autoBackupDir = m_autoOn->isChecked() ? QDir::fromNativeSeparators(m_autoDir->text()) : QString();
    st.autoBackupKeep = m_autoKeep->value();
    Store::instance().saveSettings(st);
}

void BackupPage::backupNow(bool includeReceipts)
{
    Store &s = Store::instance();
    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Save backup"),
                                                      QDir(docsDir()).filePath(backup::defaultFileName(s, includeReceipts)),
                                                      QStringLiteral("Ledgerly backup (*.json)"));
    if (path.isEmpty()) return;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    QString err;
    int receipts = 0;
    const bool ok = backup::writeFile(s, path, includeReceipts, &err, &receipts);
    QApplication::restoreOverrideCursor();
    if (!ok) {
        QMessageBox::warning(this, QStringLiteral("Backup failed"), err);
        return;
    }
    QMessageBox::information(this, QStringLiteral("Backup saved"),
                             QStringLiteral("Backup saved%1 to:\n%2").arg(includeReceipts ? QStringLiteral(" with %1 receipt(s)").arg(receipts) : QString(),
                                                                       QDir::toNativeSeparators(path)));
    refresh();
}

void BackupPage::exportCsv()
{
    Store &s = Store::instance();
    if (s.transactions().isEmpty()) {
        QMessageBox::information(this, QStringLiteral("Export"), QStringLiteral("There are no transactions to export."));
        return;
    }
    const QString path = ui::askExportPath(this, QStringLiteral("Export all transactions"),
                                           QStringLiteral("%1-transactions-%2").arg(util::slug(s.settings().businessName), QDate::currentDate().toString(Qt::ISODate)));
    if (path.isEmpty()) return;
    QString err;
    if (!backup::exportTransactions(s, s.transactions(), path, QStringLiteral("All transactions"), &err))
        QMessageBox::warning(this, QStringLiteral("Export failed"), err);
}

void BackupPage::chooseRestoreFile()
{
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Choose a backup file"), docsDir(), QStringLiteral("Ledgerly backup (*.json);;All files (*)"));
    if (!path.isEmpty()) loadRestoreFile(path);
}

bool BackupPage::loadRestoreFile(const QString &path)
{
    m_hasPending = false;
    m_restoreOptions->hide();
    m_restoreInfo->show();
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        m_restoreInfo->setText(ui::colorSpan(QStringLiteral("Can't open the file: %1").arg(f.errorString()), Theme::c().badText, true));
        return false;
    }
    QString err;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const bool ok = backup::parse(f.readAll(), &m_pending, &err);
    QApplication::restoreOverrideCursor();
    if (!ok) {
        m_restoreInfo->setText(ui::colorSpan(QStringLiteral("Can't restore this file. %1").arg(err), Theme::c().badText, true));
        return false;
    }
    m_pendingName = QFileInfo(path).fileName();
    m_hasPending = true;
    m_restoreInfo->setText(QStringLiteral("<b>%1</b> — %2%3<br>%4 categories · %5 transactions · %6 receipts")
                               .arg(m_pendingName.toHtmlEscaped(), m_pending.settings.businessName.toHtmlEscaped(),
                                    m_pending.exportedAt.isEmpty() ? QString() : QStringLiteral(", created %1").arg(util::fmtDateTime(m_pending.exportedAt)))
                               .arg(m_pending.categories.size())
                               .arg(m_pending.transactions.size())
                               .arg(m_pending.receipts.size()));
    m_restoreOptions->show();
    return true;
}

void BackupPage::restore()
{
    if (!m_hasPending) return;
    Store &s = Store::instance();
    QString err;
    if (m_replace->isChecked()) {
        const auto r = QMessageBox::warning(this, QStringLiteral("Replace all data?"),
                                            QStringLiteral("This will permanently replace your current %1 transactions and %2 categories with the backup.\n\n"
                                                           "Consider making a backup of the current data first.")
                                                .arg(s.transactions().size())
                                                .arg(s.categories().size()),
                                            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
        if (r != QMessageBox::Yes) return;
        QApplication::setOverrideCursor(Qt::WaitCursor);
        const bool ok = s.replaceAll(m_pending, &err);
        QApplication::restoreOverrideCursor();
        if (!ok) {
            QMessageBox::warning(this, QStringLiteral("Restore failed"), err);
            return;
        }
        QMessageBox::information(this, QStringLiteral("Restored"), QStringLiteral("The backup was restored."));
    } else {
        MergeStats st;
        QApplication::setOverrideCursor(Qt::WaitCursor);
        const bool ok = s.merge(m_pending, &st, &err);
        QApplication::restoreOverrideCursor();
        if (!ok) {
            QMessageBox::warning(this, QStringLiteral("Merge failed"), err);
            return;
        }
        QMessageBox::information(this, QStringLiteral("Merged"),
                                 QStringLiteral("Added %1 transactions, %2 categories and %3 receipts.").arg(st.transactions).arg(st.categories).arg(st.receipts));
    }
    m_hasPending = false;
    m_pending = BackupData();
    m_restoreOptions->hide();
    m_restoreInfo->hide();
}
