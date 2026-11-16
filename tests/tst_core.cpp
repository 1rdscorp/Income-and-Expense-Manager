#include "core/Backup.h"
#include "core/Defaults.h"
#include "core/Reports.h"
#include "core/Store.h"
#include "core/Util.h"

#include <QTemporaryDir>
#include <QtTest>

class CoreTest : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_dir;
    Store &store() { return Store::instance(); }

    QString catId(const QString &name)
    {
        for (const auto &c : store().categories())
            if (c.name == name) return c.id;
        return {};
    }

    Transaction tx(const QString &cat, qint64 amount, const QDate &date)
    {
        Transaction t;
        t.categoryId = catId(cat);
        t.amount = amount;
        t.date = date;
        t.description = QStringLiteral("test");
        return t;
    }

private slots:
    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
        QString err;
        QVERIFY2(store().open(m_dir.path(), &err), qPrintable(err));
    }

    void parseAmount()
    {
        QCOMPARE(util::parseAmount(QStringLiteral("1,234.56")), 123456);
        QCOMPARE(util::parseAmount(QStringLiteral("1234,5")), 123450);
        QCOMPARE(util::parseAmount(QStringLiteral("1.234,56")), 123456);
        QCOMPARE(util::parseAmount(QStringLiteral("$12")), 1200);
        QCOMPARE(util::parseAmount(QStringLiteral("0.1")), 10);
        QCOMPARE(util::parseAmount(QStringLiteral("abc")), -1);
        QCOMPARE(util::parseAmount(QString()), -1);
        QCOMPARE(util::amountPlain(123456), QStringLiteral("1234.56"));
        QCOMPARE(util::amountPlain(-5), QStringLiteral("-0.05"));
    }

    void csvEscaping()
    {
        const QString csv = util::toCsv({{QStringLiteral("a,b"), QStringLiteral("say \"hi\""), QStringLiteral("plain")}});
        QCOMPARE(csv, QString(QChar(0xFEFF)) + QStringLiteral("\"a,b\",\"say \"\"hi\"\"\",plain\r\n"));
    }

    void seededCategories()
    {
        QCOMPARE(store().categories().size(), defaults::categories().size());
        QCOMPARE(store().categoriesOfType(TxType::Income).size(), 7);
        QCOMPARE(store().categoriesOfType(TxType::Expense).size(), 21);
    }

    void addTransactionsAndTotals()
    {
        const QDate d(2026, 3, 15);
        Receipt r;
        r.name = QStringLiteral("r.png");
        r.mime = QStringLiteral("image/png");
        r.data = QByteArray("\x89PNG fake", 9);
        QString err;
        QVERIFY2(store().saveTransaction(tx(QStringLiteral("Sales Revenue"), 500000, d), {}, {}, &err), qPrintable(err));
        QVERIFY2(store().saveTransaction(tx(QStringLiteral("Office Supplies"), 123456, d), {r}, {}, &err), qPrintable(err));
        QVERIFY(store().saveTransaction(tx(QStringLiteral("Rent & Lease"), 100000, QDate(2026, 4, 1)), {}, {}, &err));
        QVERIFY(store().saveTransaction(tx(QStringLiteral("Rent & Lease"), 90000, QDate(2025, 4, 1)), {}, {}, &err));

        // invalid input is rejected
        QVERIFY(!store().saveTransaction(tx(QStringLiteral("Rent & Lease"), 0, d), {}, {}, &err));

        const Totals m = Store::totals(store().inMonth(2026, 3));
        QCOMPARE(m.income, 500000);
        QCOMPARE(m.expense, 123456);
        QCOMPARE(m.net(), 376544);
        const Totals y = Store::totals(store().inYear(2026));
        QCOMPARE(y.expense, 223456);
        QCOMPARE(store().monthly(2026)[3].expense, 100000);
        QCOMPARE(store().yearsWithData().first(), 2025);
        QCOMPARE(store().totalReceipts(), 1);
        // income category forces income type
        QCOMPARE(store().transactions().last().type, TxType::Expense);
    }

    void reportsBuild()
    {
        const auto m = reports::monthly(store(), 2026, 3);
        QCOMPARE(m.tables.size(), 3);
        QCOMPARE(m.tables[1].rows.size(), 2); // Office Supplies + Total
        const auto a = reports::annual(store(), 2026);
        QVERIFY(a.chart.valid);
        QCOMPARE(a.chart.expense[3], 100000);
        QVERIFY(a.tables[0].kinds.contains(reports::RowKind::Net));
        const auto y = reports::yearOverYear(store());
        QCOMPARE(y.tables[0].rows.size(), store().yearsWithData().size());
        QVERIFY(reports::toHtml(a, QStringLiteral("Biz <&>")).contains(QStringLiteral("Biz &lt;&amp;&gt;")));
    }

    void categoryLifecycle()
    {
        Category c;
        c.name = QStringLiteral("Cleaning");
        c.type = TxType::Expense;
        c.budget = 10000;
        QString err;
        QVERIFY(store().saveCategory(c, &err));
        Category dup = c;
        dup.name = QStringLiteral("cleaning");
        QVERIFY(!store().saveCategory(dup, &err)); // duplicate name per type

        const QString office = catId(QStringLiteral("Office Supplies"));
        const QString cleaning = catId(QStringLiteral("Cleaning"));
        QVERIFY(store().deleteCategory(office, cleaning, &err));
        QVERIFY(catId(QStringLiteral("Office Supplies")).isEmpty());
        int moved = 0;
        for (const auto &t : store().transactions())
            if (t.categoryId == cleaning) ++moved;
        QCOMPARE(moved, 1);
        QCOMPARE(store().totalReceipts(), 1); // receipts survive a move
    }

    void backupRoundTrip()
    {
        const QByteArray json = backup::toJson(store(), true);
        BackupData d;
        QString err;
        QVERIFY2(backup::parse(json, &d, &err), qPrintable(err));
        QCOMPARE(d.transactions.size(), store().transactions().size());
        QCOMPARE(d.categories.size(), store().categories().size());
        QCOMPARE(d.receipts.size(), 1);
        QCOMPARE(d.receipts[0].data, QByteArray("\x89PNG fake", 9));

        QVERIFY(store().eraseAll(&err));
        QCOMPARE(store().transactions().size(), 0);
        QCOMPARE(store().categories().size(), defaults::categories().size());

        QVERIFY(store().replaceAll(d, &err));
        QCOMPARE(store().transactions().size(), d.transactions.size());
        QCOMPARE(store().totalReceipts(), 1);
        QVERIFY(!catId(QStringLiteral("Cleaning")).isEmpty());

        // merging the same backup again adds nothing
        MergeStats st;
        QVERIFY(store().merge(d, &st, &err));
        QCOMPARE(st.transactions, 0);
        QCOMPARE(st.receipts, 0);
    }

    void parsesWebAppBackup()
    {
        const QByteArray web = R"({"app":"ledgerly","version":1,"settings":{"businessName":"Web Biz","currency":"EUR"},
          "categories":[{"id":"c1","name":"Sales Revenue","type":"income","icon":"x","budget":0}],
          "transactions":[{"id":"t1","type":"income","categoryId":"c1","amount":1999,"date":"2026-01-02"},
                          {"id":"bad","type":"income","categoryId":"c1","amount":5,"date":"not-a-date"}],
          "receipts":[{"id":"r1","transactionId":"t1","name":"a.txt","data":"data:text/plain;base64,aGVsbG8="}]})";
        BackupData d;
        QString err;
        QVERIFY2(backup::parse(web, &d, &err), qPrintable(err));
        QCOMPARE(d.settings.businessName, QStringLiteral("Web Biz"));
        QCOMPARE(d.settings.currency, QStringLiteral("EUR"));
        QCOMPARE(d.transactions.size(), 1);
        QCOMPARE(d.receipts[0].data, QByteArray("hello"));
        QCOMPARE(d.receipts[0].mime, QStringLiteral("text/plain"));

        QVERIFY(!backup::parse("{\"app\":\"other\"}", &d, &err));
        QVERIFY(!backup::parse("not json", &d, &err));

        // merge maps the duplicate "Sales Revenue" category onto the existing one
        MergeStats st;
        QVERIFY(store().merge(d, &st, &err));
        QCOMPARE(st.categories, 0);
        QCOMPARE(st.transactions, 1);
        const Transaction *t = store().transaction(QStringLiteral("t1"));
        QVERIFY(t);
        QCOMPARE(t->categoryId, catId(QStringLiteral("Sales Revenue")));
    }

    void sampleData()
    {
        QString err;
        QVERIFY(store().eraseAll(&err));
        QVERIFY(store().loadSampleData() > 100);
        QCOMPARE(store().loadSampleData(), 0); // only once
    }

    void cleanupTestCase() { store().close(); }
};

QTEST_GUILESS_MAIN(CoreTest)
#include "tst_core.moc"
