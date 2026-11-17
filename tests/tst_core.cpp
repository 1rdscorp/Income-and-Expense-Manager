#include "core/Backup.h"
#include "core/Defaults.h"
#include "core/Recurrence.h"
#include "core/Reports.h"
#include "core/Store.h"
#include "core/Util.h"

#include <QSqlDatabase>
#include <QSqlQuery>
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


    void recurrenceMath()
    {
        // Monthly on the 31st clamps to short months but returns to the 31st afterwards
        QDate d(2026, 1, 31);
        d = recurrence::advance(d, Frequency::Monthly, 31);
        QCOMPARE(d, QDate(2026, 2, 28));
        d = recurrence::advance(d, Frequency::Monthly, 31);
        QCOMPARE(d, QDate(2026, 3, 31));
        QCOMPARE(recurrence::advance(QDate(2028, 2, 29), Frequency::Yearly, 29), QDate(2029, 2, 28));
        QCOMPARE(recurrence::advance(QDate(2026, 11, 15), Frequency::Quarterly, 15), QDate(2027, 2, 15));
        QCOMPARE(recurrence::advance(QDate(2026, 11, 17), Frequency::Weekly, 17), QDate(2026, 11, 24));

        Recurring r;
        r.amount = 12000;
        r.frequency = Frequency::Weekly;
        QCOMPARE(recurrence::monthlyEquivalent(r), 52000);
        r.frequency = Frequency::Yearly;
        QCOMPARE(recurrence::monthlyEquivalent(r), 1000);
        r.frequency = Frequency::Monthly;
        r.day = 1;
        QCOMPARE(recurrence::describe(r), QStringLiteral("Monthly on the 1st"));
        r.day = 22;
        QCOMPARE(recurrence::describe(r), QStringLiteral("Monthly on the 22nd"));
        r.day = 13;
        QCOMPARE(recurrence::describe(r), QStringLiteral("Monthly on the 13th"));
    }

    void recurringCatchUpAndSchedule()
    {
        Store &s = store();
        const int before = s.transactions().size();
        Recurring rent;
        rent.categoryId = catId(QStringLiteral("Rent & Lease"));
        rent.amount = 180000;
        rent.description = QStringLiteral("Shop rent");
        rent.startDate = QDate(2026, 8, 1);
        QString err;
        // "today" is 17 Nov: Aug, Sep, Oct and Nov payments are due and get recorded at once
        QVERIFY2(s.saveRecurring(rent, &err, QDate(2026, 11, 17)), qPrintable(err));
        QCOMPARE(s.recurring().size(), 1);
        const Recurring r = s.recurring().first();
        QCOMPARE(r.nextDate, QDate(2026, 12, 1));
        QCOMPARE(r.day, 1);
        QCOMPARE(s.recurringPostedCount(r.id), 4);
        QCOMPARE(s.transactions().size(), before + 4);
        QCOMPARE(s.autoPosted().size(), 0); // user-created catch-up is not an "automatic" notice

        // Nothing more until the next due date; then exactly one
        QCOMPARE(s.processRecurring(QDate(2026, 11, 30)).size(), 0);
        const auto posted = s.processRecurring(QDate(2026, 12, 1));
        QCOMPARE(posted.size(), 1);
        QCOMPARE(posted[0].date, QDate(2026, 12, 1));
        QCOMPARE(posted[0].recurringId, r.id);
        QCOMPARE(posted[0].type, TxType::Expense);
        QCOMPARE(s.autoPosted().size(), 1);
        s.clearAutoPosted();

        // Upcoming list
        const auto up = s.upcoming(QDate(2026, 12, 2), QDate(2027, 3, 1));
        QCOMPARE(up.size(), 3); // Jan, Feb, Mar
        QCOMPARE(up.first().first, QDate(2027, 1, 1));

        // Pausing stops payments; resuming skips the paused period
        QVERIFY(s.setRecurringActive(r.id, false));
        QCOMPARE(s.processRecurring(QDate(2027, 3, 10)).size(), 0);
        QVERIFY(s.setRecurringActive(r.id, true, QDate(2027, 3, 10)));
        QCOMPARE(s.recurringRule(r.id)->nextDate, QDate(2027, 4, 1));

        // Editing the amount keeps the schedule; past payments are untouched
        Recurring edit = *s.recurringRule(r.id);
        edit.amount = 190000;
        QVERIFY(s.saveRecurring(edit, &err, QDate(2027, 3, 10)));
        QCOMPARE(s.recurringRule(r.id)->nextDate, QDate(2027, 4, 1));
        QCOMPARE(s.processRecurring(QDate(2027, 4, 1)).first().amount, 190000);

        // End date stops the series
        edit = *s.recurringRule(r.id);
        edit.endDate = QDate(2027, 5, 15);
        QVERIFY(s.saveRecurring(edit, &err, QDate(2027, 4, 2)));
        QCOMPARE(s.processRecurring(QDate(2027, 12, 31)).size(), 1); // only May
        QVERIFY(s.recurringRule(r.id)->finished());

        // Invalid rules are rejected
        Recurring bad;
        bad.categoryId = catId(QStringLiteral("Utilities"));
        bad.startDate = QDate(2026, 1, 1);
        QVERIFY(!s.saveRecurring(bad, &err)); // no amount
        bad.amount = 100;
        bad.endDate = QDate(2025, 1, 1);
        QVERIFY(!s.saveRecurring(bad, &err)); // ends before it starts
    }

    void recurringSurvivesBackupAndCategoryMove()
    {
        Store &s = store();
        QString err;
        Recurring net;
        net.categoryId = catId(QStringLiteral("Telephone & Internet"));
        net.amount = 8900;
        net.description = QStringLiteral("Internet");
        net.startDate = QDate(2026, 11, 5);
        QVERIFY(s.saveRecurring(net, &err, QDate(2026, 11, 17)));
        const int rules = s.recurring().size();

        BackupData d;
        QVERIFY(backup::parse(backup::toJson(s, false), &d, &err));
        QCOMPARE(d.recurring.size(), rules);
        int linked = 0;
        for (const auto &t : d.transactions)
            if (!t.recurringId.isEmpty()) ++linked;
        QVERIFY(linked >= 6);

        // Moving a category moves its recurring rules too
        Category phone;
        phone.name = QStringLiteral("Phone & Data");
        QVERIFY(s.saveCategory(phone, &err));
        const QString phoneId = catId(QStringLiteral("Phone & Data"));
        QVERIFY(s.deleteCategory(catId(QStringLiteral("Telephone & Internet")), phoneId, &err));
        bool moved = false;
        for (const auto &r : s.recurring())
            if (r.description == QLatin1String("Internet")) moved = r.categoryId == phoneId;
        QVERIFY(moved);

        // Deleting a rule keeps its recorded payments
        QString netId;
        for (const auto &r : s.recurring())
            if (r.description == QLatin1String("Internet")) netId = r.id;
        const int txBefore = s.transactions().size();
        QVERIFY(s.deleteRecurring(netId, &err));
        QCOMPARE(s.transactions().size(), txBefore);
        QCOMPARE(s.recurringPostedCount(netId), 0);

        // Restore brings the rules back
        QVERIFY(s.replaceAll(d, &err));
        QCOMPARE(s.recurring().size(), rules);
    }

    void upgradesVersion10Database()
    {
        // A v1.0 database has no recurring table or recurring_id column
        QTemporaryDir dir;
        {
            QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("old"));
            db.setDatabaseName(dir.filePath(QStringLiteral("ledgerly.db")));
            QVERIFY(db.open());
            QSqlQuery q(db);
            QVERIFY(q.exec(QStringLiteral("CREATE TABLE meta (key TEXT PRIMARY KEY, value TEXT)")));
            QVERIFY(q.exec(QStringLiteral("INSERT INTO meta VALUES('initialized','1')")));
            QVERIFY(q.exec(QStringLiteral("CREATE TABLE categories (id TEXT PRIMARY KEY, name TEXT NOT NULL, type TEXT NOT NULL, icon TEXT, budget INTEGER NOT NULL DEFAULT 0, created_at TEXT)")));
            QVERIFY(q.exec(QStringLiteral("INSERT INTO categories VALUES('c1','Rent','expense','x',0,'')")));
            QVERIFY(q.exec(QStringLiteral("CREATE TABLE transactions (id TEXT PRIMARY KEY, type TEXT NOT NULL, category_id TEXT, amount INTEGER NOT NULL, date TEXT NOT NULL, description TEXT, vendor TEXT, payment_method TEXT, notes TEXT, created_at TEXT, updated_at TEXT)")));
            QVERIFY(q.exec(QStringLiteral("INSERT INTO transactions VALUES('t1','expense','c1',5000,'2026-10-01','old','','','','','')")));
            db.close();
        }
        QSqlDatabase::removeDatabase(QStringLiteral("old"));
        const QString mainDir = store().dataDir();
        QString err;
        QVERIFY2(store().open(dir.path(), &err), qPrintable(err));
        QCOMPARE(store().transactions().size(), 1);
        QCOMPARE(store().transactions().first().description, QStringLiteral("old"));
        Recurring r;
        r.categoryId = QStringLiteral("c1");
        r.amount = 5000;
        r.startDate = QDate(2026, 11, 1);
        QVERIFY2(store().saveRecurring(r, &err, QDate(2026, 11, 17)), qPrintable(err));
        QCOMPARE(store().transactions().size(), 2);
        // reopen the main test database
        QVERIFY(store().open(mainDir, &err));
    }

    void sampleData()
    {
        QString err;
        QVERIFY(store().eraseAll(&err));
        QVERIFY(store().loadSampleData() > 100);
        QCOMPARE(store().recurring().size(), 6);
        for (const auto &r : store().recurring()) QVERIFY(r.nextDate > QDate::currentDate());
        QCOMPARE(store().loadSampleData(), 0); // only once
    }

    void cleanupTestCase() { store().close(); }
};

QTEST_GUILESS_MAIN(CoreTest)
#include "tst_core.moc"
