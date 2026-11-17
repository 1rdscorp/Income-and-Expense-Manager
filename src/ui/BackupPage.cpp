#include "BackupPage.h"

#include "Theme.h"
#include "Widgets.h"
#include "core/Backup.h"
#include "core/Store.h"
#include "core/Sync.h"
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
#include <QDialog>
#include <QDialogButtonBox>
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

    // Shared folder (several computers)
    auto *shared = new Card(QStringLiteral("Shared folder — use Ledgerly on several computers"));
    m_syncStatus = new QLabel;
    m_syncStatus->setWordWrap(true);
    m_syncStatus->setTextFormat(Qt::RichText);
    m_syncStatus->setTextInteractionFlags(Qt::TextSelectableByMouse);
    shared->body()->addWidget(m_syncStatus);
    auto *srow = new QHBoxLayout;
    m_syncSetup = ui::button(QStringLiteral("Set up shared folder…"), QStringLiteral("cloud"), QStringLiteral("primary"));
    m_syncNow = ui::button(QStringLiteral("Sync now"), QStringLiteral("refresh"));
    m_syncOpen = ui::button(QStringLiteral("Open folder"), QStringLiteral("external"));
    m_syncDisconnect = ui::button(QStringLiteral("Disconnect"), QStringLiteral("x"), QStringLiteral("dangerText"));
    srow->addWidget(m_syncSetup);
    srow->addWidget(m_syncNow);
    srow->addWidget(m_syncOpen);
    srow->addWidget(m_syncDisconnect);
    srow->addStretch(1);
    shared->body()->addLayout(srow);
    shared->body()->addWidget(ui::muted(QStringLiteral(
        "Pick a folder that is synced on every computer — e.g. “My Drive\\Ledgerly” in Google Drive for desktop, OneDrive or Dropbox. "
        "Each computer keeps its own copy of the data; only changes go through the folder, so it is safe with cloud-sync tools. "
        "Changes arrive on the other computers within a few minutes of the cloud service syncing them. Keep making backups too.")));
    root->addWidget(shared);
    connect(m_syncSetup, &QPushButton::clicked, this, &BackupPage::setupSharedFolder);
    connect(m_syncNow, &QPushButton::clicked, this, [this]() {
        QApplication::setOverrideCursor(Qt::WaitCursor);
        const sync::Result r = sync::syncNow(Store::instance());
        QApplication::restoreOverrideCursor();
        if (!r.ok) QMessageBox::warning(this, QStringLiteral("Sync"), r.error);
        refresh();
    });
    connect(m_syncOpen, &QPushButton::clicked, this, []() {
        QDesktopServices::openUrl(QUrl::fromLocalFile(sync::folder(Store::instance())));
    });
    connect(m_syncDisconnect, &QPushButton::clicked, this, [this]() {
        if (QMessageBox::question(this, QStringLiteral("Disconnect shared folder"),
                                  QStringLiteral("Stop sharing with the other computers?\n\nThis computer keeps all its data. The shared folder and "
                                                 "the other computers are not changed. You can connect again later.")) != QMessageBox::Yes)
            return;
        sync::disconnect(Store::instance());
        refresh();
    });

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

    // Shared folder status
    const bool connected = sync::isConnected(s);
    m_syncSetup->setVisible(!connected);
    m_syncNow->setVisible(connected);
    m_syncOpen->setVisible(connected);
    m_syncDisconnect->setVisible(connected);
    if (!connected) {
        m_syncStatus->setText(QStringLiteral("<b>Not connected.</b> This computer's data is only on this computer."));
    } else {
        const QDateTime last = sync::lastSync(s);
        const QString err = sync::lastError(s);
        const QStringList others = sync::computers(s);
        QString html = QStringLiteral("<b>Connected</b> to <code>%1</code><br>").arg(QDir::toNativeSeparators(sync::folder(s)).toHtmlEscaped());
        html += err.isEmpty() ? QStringLiteral("Last synced %1").arg(last.isValid() ? util::fmtDateTime(last.toString(Qt::ISODateWithMs)) : QStringLiteral("never"))
                              : ui::colorSpan(err, Theme::c().badText, true);
        const int pending = s.pendingChangeCount();
        if (pending) html += QStringLiteral(" · %1 change%2 waiting to be shared").arg(pending).arg(pending == 1 ? QString() : QStringLiteral("s"));
        html += QStringLiteral("<br>This computer: <b>%1</b>").arg(sync::computerName().toHtmlEscaped());
        html += others.isEmpty() ? QStringLiteral(" · no other computers yet — set up Ledgerly on another computer and choose this same folder.")
                                 : QStringLiteral(" · also used by: %1").arg(others.join(QStringLiteral(", ")).toHtmlEscaped());
        m_syncStatus->setText(html);
    }
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
                                                           "Consider making a backup of the current data first.%3")
                                                .arg(s.transactions().size())
                                                .arg(s.categories().size())
                                                .arg(sync::isConnected(s) ? QStringLiteral("\n\nThis computer uses a shared folder: the restored data will also replace the data on the other computers.") : QString()),
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

void BackupPage::setupSharedFolder()
{
    Store &s = Store::instance();
    const QString start = QDir::homePath();
    const QString folder = QFileDialog::getExistingDirectory(this, QStringLiteral("Choose the shared folder (e.g. inside Google Drive)"), start);
    if (folder.isEmpty()) return;
    const sync::FolderInfo info = sync::inspect(folder);
    if (!info.exists) {
        QMessageBox::warning(this, QStringLiteral("Shared folder"), info.error);
        return;
    }
    sync::JoinMode mode = sync::JoinMode::Create;
    if (!info.hasLedger) {
        const auto r = QMessageBox::question(
            this, QStringLiteral("Create shared folder"),
            QStringLiteral("Share this computer's data through\n%1 ?\n\nThen, on each other computer, install Ledgerly and choose this same folder.")
                .arg(QDir::toNativeSeparators(folder)));
        if (r != QMessageBox::Yes) return;
    } else {
        QDialog dlg(this);
        dlg.setWindowTitle(QStringLiteral("Join shared folder"));
        dlg.setMinimumWidth(520);
        auto *l = new QVBoxLayout(&dlg);
        auto *msg = new QLabel(QStringLiteral("This folder already holds Ledgerly data shared by <b>%1</b>%2.<br><br>What should happen to the data on this computer?")
                                   .arg(info.createdBy.toHtmlEscaped(),
                                        info.computers.size() > 1 ? QStringLiteral(" (%1 computers)").arg(info.computers.size()) : QString()));
        msg->setWordWrap(true);
        l->addWidget(msg);
        auto *replace = new QRadioButton(QStringLiteral("Use the shared data — replace what's on this computer (best for a new computer)"));
        auto *merge = new QRadioButton(QStringLiteral("Combine — add this computer's transactions to the shared data"));
        const bool hasOwnData = !s.transactions().isEmpty();
        (hasOwnData ? merge : replace)->setChecked(true);
        l->addWidget(replace);
        l->addWidget(merge);
        l->addWidget(ui::muted(hasOwnData ? QStringLiteral("This computer has %1 transactions. If they were already shared from here before, choose “Use the shared data”.").arg(s.transactions().size())
                                          : QStringLiteral("This computer has no transactions yet.")));
        auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        bb->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Connect"));
        bb->button(QDialogButtonBox::Ok)->setObjectName(QStringLiteral("primary"));
        connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
        connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
        l->addWidget(bb);
        if (dlg.exec() != QDialog::Accepted) return;
        mode = replace->isChecked() ? sync::JoinMode::Replace : sync::JoinMode::Merge;
        if (mode == sync::JoinMode::Replace && hasOwnData &&
            QMessageBox::warning(this, QStringLiteral("Replace this computer's data?"),
                                 QStringLiteral("The %1 transactions on this computer will be replaced by the shared data.\n\nConsider making a backup first.")
                                     .arg(s.transactions().size()),
                                 QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes)
            return;
    }
    QApplication::setOverrideCursor(Qt::WaitCursor);
    QString err;
    const bool ok = sync::connect(s, folder, mode, &err);
    QApplication::restoreOverrideCursor();
    if (!ok) {
        QMessageBox::warning(this, QStringLiteral("Shared folder"), QStringLiteral("Couldn't connect: %1").arg(err));
        sync::disconnect(s);
    } else {
        QMessageBox::information(this, QStringLiteral("Shared folder"),
                                 mode == sync::JoinMode::Create
                                     ? QStringLiteral("Connected. Now open Ledgerly on your other computers and choose this same folder in Backup & Sync.")
                                     : QStringLiteral("Connected. This computer now has the shared data."));
    }
    refresh();
}
