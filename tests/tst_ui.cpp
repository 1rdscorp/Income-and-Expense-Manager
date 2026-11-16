#include "core/Backup.h"
#include "core/Store.h"
#include "ui/BackupPage.h"
#include "ui/CategoryDialog.h"
#include "ui/MainWindow.h"
#include "ui/ReportsPage.h"
#include "ui/Theme.h"
#include "ui/TransactionDialog.h"
#include "ui/TransactionsPage.h"

#include <QAbstractButton>
#include <QApplication>
#include <QBuffer>
#include <QComboBox>
#include <QDateEdit>
#include <QDialogButtonBox>
#include <QImage>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QTreeWidget>
#include <QtTest>

namespace {

/* Accepts the next QMessageBox that appears (Yes / OK), so modal prompts don't block tests. */
void acceptNextMessageBox()
{
    auto *timer = new QTimer;
    int *tries = new int(0);
    QObject::connect(timer, &QTimer::timeout, [timer, tries]() {
        if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
            QAbstractButton *b = box->button(QMessageBox::Yes);
            if (!b) b = box->button(QMessageBox::Ok);
            if (!b) b = box->defaultButton();
            if (b) b->click();
            else box->accept();
        } else if (++*tries < 100) {
            return;
        }
        timer->stop();
        timer->deleteLater();
        delete tries;
    });
    timer->start(20);
}

QPushButton *buttonByAccessibleName(QWidget *root, const QString &name)
{
    for (auto *b : root->findChildren<QPushButton *>())
        if (b->accessibleName() == name) return b;
    return nullptr;
}

QString categoryId(const QString &name)
{
    for (const auto &c : Store::instance().categories())
        if (c.name == name) return c.id;
    return {};
}

} // namespace

class UiTest : public QObject
{
    Q_OBJECT
    QTemporaryDir m_dir;
    QString m_png;

private slots:
    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
        QString err;
        QVERIFY2(Store::instance().open(m_dir.filePath(QStringLiteral("data")), &err), qPrintable(err));
        Theme::instance().apply(QStringLiteral("light"));
        QImage img(1600, 1200, QImage::Format_RGB32);
        img.fill(QColor(200, 120, 40));
        m_png = m_dir.filePath(QStringLiteral("receipt.png"));
        QVERIFY(img.save(m_png));
    }

    void addTransactionWithReceipt()
    {
        TransactionDialog dlg;
        dlg.setNewType(TxType::Expense);
        dlg.findChild<QLineEdit *>(QStringLiteral("amountEdit"))->setText(QStringLiteral("1,234.56"));
        auto *cat = dlg.findChild<QComboBox *>(QStringLiteral("categoryCombo"));
        const int idx = cat->findData(categoryId(QStringLiteral("Office Supplies")));
        QVERIFY(idx > 0);
        cat->setCurrentIndex(idx);
        dlg.findChild<QLineEdit *>(QStringLiteral("descEdit"))->setText(QStringLiteral("Printer paper"));
        dlg.findChild<QLineEdit *>(QStringLiteral("vendorEdit"))->setText(QStringLiteral("OfficeMart"));
        QCOMPARE(dlg.addFiles({m_png}), 1);
        buttonByAccessibleName(&dlg, QStringLiteral("save"))->click();
        QCOMPARE(dlg.outcome(), TransactionDialog::Saved);

        const auto &txs = Store::instance().transactions();
        QCOMPARE(txs.size(), 1);
        QCOMPARE(txs[0].amount, 123456);
        QCOMPARE(txs[0].description, QStringLiteral("Printer paper"));
        QCOMPARE(txs[0].date, QDate::currentDate());
        QCOMPARE(Store::instance().receiptCount(txs[0].id), 1);
        const auto receipts = Store::instance().receiptsFor(txs[0].id);
        QCOMPARE(receipts[0].name, QStringLiteral("receipt.png")); // small PNG not recompressed
    }

    void incomeTypeSwitch()
    {
        TransactionDialog dlg;
        dlg.setNewType(TxType::Income);
        dlg.findChild<QLineEdit *>(QStringLiteral("amountEdit"))->setText(QStringLiteral("5000"));
        auto *cat = dlg.findChild<QComboBox *>(QStringLiteral("categoryCombo"));
        QVERIFY(cat->findData(categoryId(QStringLiteral("Office Supplies"))) < 0); // only income categories offered
        cat->setCurrentIndex(cat->findData(categoryId(QStringLiteral("Sales Revenue"))));
        buttonByAccessibleName(&dlg, QStringLiteral("saveAnother"))->click();
        QCOMPARE(dlg.outcome(), TransactionDialog::SavedAddAnother);
        QCOMPARE(dlg.savedType(), TxType::Income);
        QCOMPARE(Store::totals(Store::instance().transactions()).income, 500000);
    }

    void editRemovesReceipt()
    {
        Transaction t;
        for (const auto &x : Store::instance().transactions())
            if (x.type == TxType::Expense) t = x;
        TransactionDialog dlg;
        dlg.setTransaction(t);
        auto *list = dlg.findChild<QListWidget *>();
        QVERIFY(list);
        QCOMPARE(list->count(), 1);
        list->item(0)->setSelected(true);
        for (auto *b : dlg.findChildren<QPushButton *>())
            if (b->text() == QStringLiteral("Remove")) b->click();
        dlg.findChild<QLineEdit *>(QStringLiteral("amountEdit"))->setText(QStringLiteral("99.99"));
        buttonByAccessibleName(&dlg, QStringLiteral("save"))->click();
        QCOMPARE(Store::instance().transaction(t.id)->amount, 9999);
        QCOMPARE(Store::instance().receiptCount(t.id), 0);
        QCOMPARE(Store::instance().totalReceipts(), 0);
    }

    void largePhotoIsCompressed()
    {
        QImage img(2400, 1800, QImage::Format_RGB32);
        quint32 seed = 12345; // noisy, photo-like content so the PNG is several MB
        for (int y = 0; y < img.height(); ++y)
            for (int x = 0; x < img.width(); ++x) {
                seed = seed * 1664525u + 1013904223u;
                img.setPixel(x, y, qRgb(int(seed >> 24), int((seed >> 16) & 0xff), int((seed >> 8) & 0xff)));
            }
        const QString big = m_dir.filePath(QStringLiteral("photo.png"));
        QVERIFY(img.save(big));
        TransactionDialog dlg;
        dlg.setNewType(TxType::Expense);
        dlg.findChild<QLineEdit *>(QStringLiteral("amountEdit"))->setText(QStringLiteral("12"));
        auto *cat = dlg.findChild<QComboBox *>(QStringLiteral("categoryCombo"));
        cat->setCurrentIndex(cat->findData(categoryId(QStringLiteral("Travel"))));
        QCOMPARE(dlg.addFiles({big}), 1);
        buttonByAccessibleName(&dlg, QStringLiteral("save"))->click();
        QString id;
        for (const auto &x : Store::instance().transactions())
            if (x.amount == 1200) id = x.id;
        const auto r = Store::instance().receiptsFor(id);
        QCOMPARE(r.size(), 1);
        QCOMPARE(r[0].mime, QStringLiteral("image/jpeg"));
        QCOMPARE(r[0].name, QStringLiteral("photo.jpg"));
        QImage back;
        QVERIFY(back.loadFromData(r[0].data));
        QCOMPARE(std::max(back.width(), back.height()), 2000);
    }

    void categoryDialog()
    {
        CategoryDialog dlg;
        dlg.setNew(TxType::Expense);
        dlg.findChild<QLineEdit *>(QStringLiteral("nameEdit"))->setText(QStringLiteral("Cleaning"));
        dlg.findChild<QLineEdit *>(QStringLiteral("budgetEdit"))->setText(QStringLiteral("150"));
        dlg.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save)->click();
        QCOMPARE(dlg.result(), int(QDialog::Accepted));
        const Category *c = Store::instance().category(categoryId(QStringLiteral("Cleaning")));
        QVERIFY(c);
        QCOMPARE(c->budget, 15000);
    }

    void mainWindowPages()
    {
        QCOMPARE(Store::instance().loadSampleData(), 0); // only when empty — we have data
        MainWindow w;
        w.show();
        for (int i = 0; i < MainWindow::PageCount; ++i) {
            w.showPage(i);
            QCOMPARE(w.currentPage(), i);
        }
        auto *tp = qobject_cast<TransactionsPage *>(w.pageWidget(MainWindow::Transactions));
        QDate today = QDate::currentDate();
        tp->showMonth(today.year(), today.month());
        auto *tree = tp->findChild<QTreeWidget *>();
        QCOMPARE(tree->topLevelItemCount(), 1); // one day group (today)
        QCOMPARE(tree->topLevelItem(0)->childCount(), Store::instance().transactions().size());

        // Data changes refresh the visible page
        w.showPage(MainWindow::Transactions);
        QString err;
        Transaction t;
        t.categoryId = categoryId(QStringLiteral("Rent & Lease"));
        t.amount = 100000;
        t.date = today;
        QVERIFY(Store::instance().saveTransaction(t, {}, {}, &err));
        QCOMPARE(tree->topLevelItem(0)->childCount(), Store::instance().transactions().size());

        auto *rp = qobject_cast<ReportsPage *>(w.pageWidget(MainWindow::Reports));
        w.showPage(MainWindow::Reports);
        QVERIFY(rp->findChildren<QTableWidget *>().size() >= 2);
    }

    void backupAndRestoreThroughUi()
    {
        Store &s = Store::instance();
        const QString file = m_dir.filePath(QStringLiteral("b.json"));
        QString err;
        QVERIFY(backup::writeFile(s, file, true, &err));
        const int txCount = s.transactions().size();
        QVERIFY(s.eraseAll(&err));
        QCOMPARE(s.transactions().size(), 0);

        BackupPage page;
        page.refresh();
        QVERIFY(page.loadRestoreFile(file));
        for (auto *b : page.findChildren<QPushButton *>())
            if (b->text() == QStringLiteral("Restore")) {
                acceptNextMessageBox(); // "Replace all data?"
                QTimer::singleShot(400, []() { acceptNextMessageBox(); }); // "Restored"
                b->click();
            }
        QTRY_COMPARE(s.transactions().size(), txCount);
        QVERIFY(!categoryId(QStringLiteral("Cleaning")).isEmpty());
    }

    void cleanupTestCase() { Store::instance().close(); }
};

QTEST_MAIN(UiTest)
#include "tst_ui.moc"
#include <algorithm>
