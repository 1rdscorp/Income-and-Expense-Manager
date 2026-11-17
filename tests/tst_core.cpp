#include "core/Backup.h"
#include "core/BankImport.h"
#include "core/Defaults.h"
#include "core/Recurrence.h"
#include "core/Reports.h"
#include "core/SimpleFin.h"
#include "core/Store.h"
#include "core/Sync.h"
#include "core/Util.h"
#include "core/Xlsx.h"

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QtTest>
#include <memory>


/* Minimal stand-in for SimpleFIN Bridge, used to test the client end to end. */
class MockBridge : public QObject
{
public:
    QTcpServer server;
    int accountRequests = 0;
    QString lastQuery;
    MockBridge()
    {
        server.listen(QHostAddress::LocalHost);
        QObject::connect(&server, &QTcpServer::newConnection, this, [this]() {
            while (QTcpSocket *s = server.nextPendingConnection()) {
                auto buf = std::make_shared<QByteArray>();
                QObject::connect(s, &QTcpSocket::readyRead, s, [this, s, buf]() {
                    buf->append(s->readAll());
                    if (!buf->contains("\r\n\r\n")) return;
                    handle(s, *buf);
                });
            }
        });
    }
    QString base() const { return QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort()); }
    QString token(const QString &name) const { return QString::fromLatin1((base() + QStringLiteral("/claim/") + name).toUtf8().toBase64()); }

private:
    void reply(QTcpSocket *s, int status, const QByteArray &body)
    {
        s->write("HTTP/1.1 " + QByteArray::number(status) + " X\r\nContent-Type: application/json\r\nContent-Length: " +
                 QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
        s->disconnectFromHost();
    }
    void handle(QTcpSocket *s, const QByteArray &req)
    {
        const QList<QByteArray> first = req.left(req.indexOf("\r\n")).split(' ');
        const QByteArray method = first.value(0), target = first.value(1);
        if (method == "POST" && target == "/claim/good") {
            const QString access = QStringLiteral("http://demo:s3cret@127.0.0.1:%1/simplefin").arg(server.serverPort());
            return reply(s, 200, access.toUtf8());
        }
        if (method == "POST") return reply(s, 403, "Forbidden");
        if (method == "GET" && target.startsWith("/simplefin/accounts")) {
            if (!req.contains("Authorization: Basic " + QByteArray("demo:s3cret").toBase64())) return reply(s, 403, "Forbidden");
            ++accountRequests;
            lastQuery = QString::fromUtf8(target.mid(target.indexOf('?') + 1));
            const qint64 d1 = QDateTime(QDate::currentDate().addDays(-3), QTime(12, 0)).toSecsSinceEpoch();
            const qint64 d2 = QDateTime(QDate::currentDate().addDays(-1), QTime(12, 0)).toSecsSinceEpoch();
            const QByteArray body = QStringLiteral(
                R"({"errors":["U.S. Bank: please re-check your connection soon"],"accounts":[
                  {"org":{"name":"U.S. Bank","domain":"usbank.com"},"id":"ACT-1","name":"Business Checking","currency":"USD",
                   "balance":"10234.10","available-balance":"10000.00","balance-date":%1,
                   "transactions":[
                     {"id":"T1","posted":%1,"amount":"-61.40","description":"SHELL OIL 57444"},
                     {"id":"T2","posted":%2,"amount":"1850.00","description":"DEPOSIT","payee":"Greenfield Ltd"},
                     {"id":"T3","posted":0,"amount":"-9.99","description":"PENDING THING","pending":true}]},
                  {"org":{"name":"U.S. Bank"},"id":"ACT-2","name":"Business Visa Card","currency":"USD","balance":"-512.00",
                   "balance-date":%2,"transactions":[{"id":"C1","posted":%2,"amount":"-12.00","description":"GOOGLE *WORKSPACE"}]}]})")
                                         .arg(d1)
                                         .arg(d2)
                                         .toUtf8();
            return reply(s, 200, body);
        }
        reply(s, 404, "Not found");
    }
};

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


    void makeRecurringFromExisting()
    {
        Store &s = store();
        QString err;
        Transaction t;
        t.categoryId = catId(QStringLiteral("Software & Subscriptions"));
        t.amount = 4900;
        t.date = QDate(2026, 8, 3);
        t.description = QStringLiteral("Accounting app");
        QVERIFY(s.saveTransaction(t, {}, {}, &err));
        QString id;
        for (const auto &x : s.transactions())
            if (x.description == QLatin1String("Accounting app")) id = x.id;

        // Without back-filling: next payment is the first one after "today"
        QString ruleId;
        QVERIFY2(s.makeRecurring(id, Frequency::Monthly, false, &err, QDate(2026, 11, 17), &ruleId), qPrintable(err));
        QCOMPARE(s.transaction(id)->recurringId, ruleId);
        QCOMPARE(s.recurringRule(ruleId)->nextDate, QDate(2026, 12, 3));
        QCOMPARE(s.recurringPostedCount(ruleId), 1);
        QVERIFY(!s.makeRecurring(id, Frequency::Monthly, false, &err)); // already recurring

        // Stop, then make it recurring again recording the missed months (Sep, Oct, Nov)
        QVERIFY(s.deleteRecurring(ruleId, &err));
        QVERIFY(s.transaction(id)->recurringId.isEmpty());
        QVERIFY(s.makeRecurring(id, Frequency::Monthly, true, &err, QDate(2026, 11, 17), &ruleId));
        QCOMPARE(s.recurringPostedCount(ruleId), 4);
        QCOMPARE(s.recurringRule(ruleId)->nextDate, QDate(2026, 12, 3));
        QVERIFY(s.deleteRecurring(ruleId, &err));
    }

    void excelExport()
    {
        // Workbook basics
        QCOMPARE(xlsx::columnName(0), QStringLiteral("A"));
        QCOMPARE(xlsx::columnName(25), QStringLiteral("Z"));
        QCOMPARE(xlsx::columnName(26), QStringLiteral("AA"));
        QCOMPARE(xlsx::sanitizeSheetName(QStringLiteral("Income/Expenses [2026]: a*very?long name over 31")), QStringLiteral("IncomeExpenses 2026 averylong n"));

        const QByteArray annual = reports::toXlsx(reports::annual(store(), 2026), QStringLiteral("Biz & <Co>"));
        QVERIFY(annual.startsWith("PK\x03\x04"));
        QVERIFY(annual.contains("xl/worksheets/sheet3.xml")); // Summary, Monthly totals, Category by month
        QVERIFY(annual.contains("[Content_Types].xml"));
        const QByteArray monthly = reports::toXlsx(reports::monthly(store(), 2026, 3), QStringLiteral("Biz"));
        QVERIFY(monthly.contains("xl/worksheets/sheet4.xml")); // Summary + 3 tables

        // Typed values back the display text
        const auto m = reports::monthly(store(), 2026, 3);
        QCOMPARE(m.tables[1].values.size(), m.tables[1].rows.size());
        QCOMPARE(m.stats[0].raw.kind, reports::Value::Kind::Money);
        QCOMPARE(reports::display(reports::Value::change(-4.04)), QStringLiteral("▼ 4.0%"));
        QCOMPARE(reports::display(reports::Value::moneyDash(0)), QStringLiteral("–"));

        // Transactions export by extension
        QTemporaryDir dir;
        QString err;
        QVERIFY(backup::exportTransactions(store(), store().transactions(), dir.filePath(QStringLiteral("t.xlsx")), QStringLiteral("All"), &err));
        QVERIFY(backup::exportTransactions(store(), store().transactions(), dir.filePath(QStringLiteral("t.csv")), QStringLiteral("All"), &err));
        QFile x(dir.filePath(QStringLiteral("t.xlsx")));
        QVERIFY(x.open(QIODevice::ReadOnly));
        QVERIFY(x.read(4) == QByteArray("PK\x03\x04"));
        QFile c(dir.filePath(QStringLiteral("t.csv")));
        QVERIFY(c.open(QIODevice::ReadOnly));
        QVERIFY(c.readAll().contains("Date,Type,Category"));
    }



    void bankStatementParsing()
    {
        const QByteArray ofx =
            "OFXHEADER:100\nDATA:OFXSGML\nVERSION:102\nCHARSET:1252\n\n<OFX><CREDITCARDMSGSRSV1><CCSTMTTRNRS><CCSTMTRS>"
            "<CCACCTFROM><ACCTID>4111XXXXXXXX1234</CCACCTFROM><BANKTRANLIST>"
            "<STMTTRN><TRNTYPE>DEBIT<DTPOSTED>20261103120000[-5:EST]<TRNAMT>-15.99<FITID>A1<NAME>NETFLIX.COM<MEMO>Monthly plan</STMTTRN>"
            "<STMTTRN><TRNTYPE>DEBIT<DTPOSTED>20261104<TRNAMT>-42.10<FITID>A2<NAME>SHELL OIL 57444<MEMO></STMTTRN>"
            "<STMTTRN><TRNTYPE>CREDIT<DTPOSTED>20261105<TRNAMT>500.00<FITID>A3<NAME>PAYMENT THANK YOU</STMTTRN>"
            "</BANKTRANLIST></CCSTMTRS></CCSTMTTRNRS></CREDITCARDMSGSRSV1></OFX>";
        auto r = bank::parseData(ofx, QStringLiteral("stmt.qfx"));
        QVERIFY2(r.ok(), qPrintable(r.error));
        QCOMPARE(r.format, QStringLiteral("OFX"));
        QVERIFY(r.looksLikeCard);
        QCOMPARE(r.lines.size(), 3);
        QCOMPARE(r.lines[0].date, QDate(2026, 11, 3));
        QCOMPARE(r.lines[0].amount, -1599);
        QCOMPARE(r.lines[0].externalId, QStringLiteral("ofx:A1"));
        QCOMPARE(r.lines[0].memo, QStringLiteral("Monthly plan"));
        QCOMPARE(r.lines[2].amount, 50000);

        const QByteArray qif = "!Type:Bank\nD11/03/2026\nT-1,250.00\nPOAKVIEW PROPERTIES\nMRent November\n^\nD11/04'26\nT3200\nPCLIENT ABC\n^\n";
        r = bank::parseData(qif, QStringLiteral("x.qif"));
        QVERIFY2(r.ok(), qPrintable(r.error));
        QCOMPARE(r.lines.size(), 2);
        QCOMPARE(r.lines[0].amount, -125000);
        QCOMPARE(r.lines[1].amount, 320000);
        QCOMPARE(r.lines[0].date, QDate(2026, 11, 3));
        QCOMPARE(r.lines[1].date, QDate(2026, 11, 4));
        QVERIFY(!r.lines[0].externalId.isEmpty()); // fingerprint

        // Plain CSV with a signed amount and a preamble line
        const QByteArray csv1 = "Account: Business Checking ****1234\nDate,Description,Amount,Balance\n2026-11-01,\"ACME WHOLESALE, INC\",-1200.50,800.00\n"
                                "2026-11-02,CLIENT PAYMENT,2000,2800.00\n2026-11-02,CLIENT PAYMENT,2000,4800.00\n";
        r = bank::parseData(csv1, QStringLiteral("a.csv"));
        QVERIFY2(r.ok(), qPrintable(r.error));
        QCOMPARE(r.lines.size(), 3);
        QCOMPARE(r.lines[0].description, QStringLiteral("ACME WHOLESALE, INC"));
        QCOMPARE(r.lines[0].amount, -120050);
        QVERIFY(r.lines[1].externalId != r.lines[2].externalId); // identical lines stay distinct
        QCOMPARE(r.csvMapping.dateFormat, QStringLiteral("yyyy-MM-dd"));

        // European CSV: semicolons, decimal commas, debit/credit columns, day-first dates
        const QByteArray csv2 = "Datum;Booking date;Details;Money out;Money in\n;13/11/2026;Coffee Shop;4,50;\n;14/11/2026;Invoice 77;;1.250,00\n";
        r = bank::parseData(csv2, QStringLiteral("b.csv"));
        QVERIFY2(r.ok(), qPrintable(r.error));
        QCOMPARE(r.csvMapping.delimiter, QLatin1Char(';'));
        QCOMPARE(r.lines.size(), 2);
        QCOMPARE(r.lines[0].date, QDate(2026, 11, 13));
        QCOMPARE(r.lines[0].amount, -450);
        QCOMPARE(r.lines[1].amount, 125000);

        // Mapping survives a JSON round trip (remembered per account)
        const bank::CsvMapping m = bank::CsvMapping::fromJson(r.csvMapping.toJson());
        QCOMPARE(m.debit, r.csvMapping.debit);
        QCOMPARE(m.dateFormat, r.csvMapping.dateFormat);

        bool ok = false;
        QCOMPARE(bank::parseSignedAmount(QStringLiteral("(1,234.56)"), &ok), -123456);
        QCOMPARE(bank::parseSignedAmount(QStringLiteral("$12.00 DR"), &ok), -1200);

        QCOMPARE(bank::cleanDescription(QStringLiteral("POS 4421 NETFLIX.COM 866-579-7172 CA")), QStringLiteral("Netflix.com"));
        QCOMPARE(bank::cleanDescription(QStringLiteral("CARD PURCHASE SHELL OIL 57444")), QStringLiteral("Shell Oil"));
        QCOMPARE(bank::payeeKey(QStringLiteral("SHELL OIL 12345 HOUSTON")), bank::payeeKey(QStringLiteral("Shell Oil 99881")));
    }

    void bankImportCategoriesAndDuplicates()
    {
        Store &s = store();
        QString err, accId;
        bank::CsvMapping none;
        Account card;
        card.name = QStringLiteral("Business Visa");
        card.kind = QStringLiteral("card");
        QVERIFY2(s.saveAccount(card, &err, &accId), qPrintable(err));
        card = *s.account(accId);
        Account dup = card;
        dup.id.clear();
        QVERIFY(!s.saveAccount(dup, &err)); // name taken

        // A manual transaction that the statement also contains
        Transaction manual;
        manual.categoryId = catId(QStringLiteral("Vehicle & Fuel"));
        manual.amount = 4210;
        manual.date = QDate(2026, 11, 5);
        manual.description = QStringLiteral("Fuel");
        QVERIFY(s.saveTransaction(manual, {}, {}, &err));

        ImportRule ignore;
        ignore.pattern = QStringLiteral("PAYMENT THANK YOU");
        ignore.ignore = true;
        QVERIFY(s.saveImportRule(ignore, &err));
        ImportRule rule;
        rule.pattern = QStringLiteral("acme");
        rule.categoryId = catId(QStringLiteral("Cost of Goods / Inventory"));
        QVERIFY(s.saveImportRule(rule, &err));

        QVector<bank::StatementLine> lines;
        auto line = [&](int d, qint64 amt, const char *desc, const char *id) {
            bank::StatementLine l;
            l.date = QDate(2026, 11, d);
            l.amount = amt;
            l.description = QString::fromUtf8(desc);
            l.externalId = QString::fromUtf8(id);
            lines << l;
        };
        line(3, -1599, "NETFLIX.COM", "ofx:A1");
        line(4, -4210, "SHELL OIL 57444", "ofx:A2");
        line(5, 50000, "PAYMENT THANK YOU", "ofx:A3");
        line(6, -99900, "ACME WHOLESALE INC", "ofx:A4");
        line(7, -2300, "SOMEWHERE UNKNOWN", "ofx:A5");

        auto items = bank::prepare(s, card, lines);
        QCOMPARE(items.size(), 5);
        QCOMPARE(items[0].status, bank::Proposed::Status::New);
        QCOMPARE(items[0].tx.categoryId, catId(QStringLiteral("Software & Subscriptions"))); // keyword
        QCOMPARE(items[0].tx.type, TxType::Expense);
        QCOMPARE(items[0].tx.description, QStringLiteral("Netflix.com"));
        QCOMPARE(items[1].status, bank::Proposed::Status::PossibleDuplicate); // matches the manual fuel entry
        QVERIFY(!items[1].include);
        QCOMPARE(items[2].status, bank::Proposed::Status::Ignored);
        QCOMPARE(items[3].tx.categoryId, catId(QStringLiteral("Cost of Goods / Inventory"))); // rule
        QCOMPARE(items[4].tx.categoryId, catId(QStringLiteral("Miscellaneous")));            // default

        // You fix the unknown one; next time it's learned from history
        items[4].tx.categoryId = catId(QStringLiteral("Repairs & Maintenance"));
        QCOMPARE(bank::commit(s, items, &err), 3);
        QCOMPARE(s.accountTransactionCount(accId), 3);
        QVERIFY(s.alreadyImported(accId, QStringLiteral("ofx:A1")));

        // Re-importing the same statement adds nothing
        items = bank::prepare(s, card, lines);
        int fresh = 0;
        for (const auto &p : items)
            if (p.include) ++fresh;
        QCOMPARE(fresh, 0);
        QCOMPARE(items[0].status, bank::Proposed::Status::Duplicate);

        // Learning: a new line from the same payee gets the category you chose
        lines.clear();
        line(20, -1800, "SOMEWHERE UNKNOWN 99812", "ofx:B1");
        items = bank::prepare(s, card, lines);
        QCOMPARE(items[0].tx.categoryId, catId(QStringLiteral("Repairs & Maintenance")));

        // Flip signs (card statements that show charges as positive)
        card.flipSigns = true;
        lines.clear();
        line(21, 2500, "UBER TRIP 0042", "ofx:C1");
        items = bank::prepare(s, card, lines);
        QCOMPARE(items[0].tx.type, TxType::Expense);
        QCOMPARE(items[0].tx.categoryId, catId(QStringLiteral("Travel")));

        // Deleting the account keeps its transactions
        QVERIFY(s.deleteAccount(accId, &err));
        QCOMPARE(s.accountTransactionCount(accId), 0);
        QVERIFY(s.transactions().size() > 3);
    }

    void bankAutoImportFolder()
    {
        Store &s = store();
        QString err, accId;
        Account bankAcc;
        bankAcc.name = QStringLiteral("Checking");
        QVERIFY(s.saveAccount(bankAcc, &err, &accId));
        QTemporaryDir watch;
        bank::setWatchFolder(s, accId, watch.path());
        QCOMPARE(bank::watchFolder(s, accId), watch.path());
        const QString file = watch.filePath(QStringLiteral("nov.csv"));
        {
            QFile f(file);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("Date,Description,Amount\n2026-11-12,ELECTRIC CO,-180.25\n2026-11-13,CUSTOMER DEPOSIT,950.00\n");
            f.close();
            QVERIFY(f.open(QIODevice::ReadWrite));
            QVERIFY(f.setFileTime(QDateTime::currentDateTime().addSecs(-120), QFileDevice::FileModificationTime));
        }
        const auto results = bank::runAutoImport(s);
        QCOMPARE(results.size(), 1);
        QCOMPARE(results[0].imported, 2);
        QVERIFY(!QFile::exists(file));
        QVERIFY(QFile::exists(watch.filePath(QStringLiteral("Imported/nov.csv"))));
        QCOMPARE(s.accountTransactionCount(accId), 2);
        QVERIFY(bank::runAutoImport(s).isEmpty()); // nothing left
    }


    void simpleFinProtocol()
    {
        // v2 shape: errlist + connections
        simplefin::AccountSet set;
        QString err;
        QVERIFY(simplefin::parseAccountSet(R"({"errlist":[{"code":"con.auth","msg":"Reconnect U.S. Bank"}],"connections":[{"conn_id":"C9","name":"U.S. Bank"}],
            "accounts":[{"id":"A","name":"Checking","conn_id":"C9","currency":"USD","balance":"5.00","transactions":[
            {"id":"x","posted":1763200000,"amount":"-1.25","description":"Coffee"}]}]})", &set, &err));
        QCOMPARE(set.errors, QStringList{QStringLiteral("Reconnect U.S. Bank")});
        QCOMPARE(set.accounts.size(), 1);
        QCOMPARE(set.accounts[0].org, QStringLiteral("U.S. Bank"));
        QCOMPARE(set.accounts[0].lines[0].amount, -125);
        QCOMPARE(set.accounts[0].lines[0].externalId, QStringLiteral("sfin:A:x"));
        QVERIFY(!simplefin::parseAccountSet("<html>", &set, &err));

        Store &s = store();
        MockBridge bridge;
        QString access;
        QVERIFY(!simplefin::claim(QStringLiteral("not a token"), &access, &err));
        QVERIFY(!simplefin::claim(bridge.token(QStringLiteral("used")), &access, &err));
        QVERIFY(err.contains(QStringLiteral("already used")));

        QVERIFY(!simplefin::isConnected(s));
        QVERIFY2(simplefin::connect(s, bridge.token(QStringLiteral("good")), &err), qPrintable(err));
        QVERIFY(simplefin::isConnected(s));
        QCOMPARE(bridge.accountRequests, 1); // balances-only read to set up accounts
        QVERIFY(bridge.lastQuery.contains(QStringLiteral("balances-only=1")));
        const QStringList linked = simplefin::linkedAccountNames(s);
        QCOMPARE(linked, (QStringList{QStringLiteral("U.S. Bank Business Checking"), QStringLiteral("U.S. Bank Business Visa Card")}));
        QString checkingId, visaId;
        for (const auto &a : s.accounts()) {
            if (a.name == QLatin1String("U.S. Bank Business Checking")) checkingId = a.id;
            if (a.name == QLatin1String("U.S. Bank Business Visa Card")) visaId = a.id;
        }
        QVERIFY(!s.account(checkingId)->isCard());
        QVERIFY(s.account(visaId)->isCard());

        simplefin::DownloadResult r = simplefin::download(s, true);
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(r.imported, 3); // pending one waits until it posts
        QVERIFY(bridge.lastQuery.contains(QStringLiteral("start-date=")));
        QCOMPARE(r.messages, QStringList{QStringLiteral("U.S. Bank: please re-check your connection soon")});
        QCOMPARE(s.accountTransactionCount(checkingId), 2);
        QCOMPARE(s.accountTransactionCount(visaId), 1);
        bool sawDeposit = false;
        for (const auto &t : s.transactions())
            if (t.accountId == checkingId && t.type == TxType::Income && t.description == QLatin1String("Greenfield Ltd")) sawDeposit = true;
        QVERIFY(sawDeposit);

        const int requests = bridge.accountRequests;
        r = simplefin::download(s); // within 6 hours: no request at all
        QCOMPARE(bridge.accountRequests, requests);
        r = simplefin::download(s, true); // forced: same transactions again are skipped
        QCOMPARE(r.imported, 0);
        QVERIFY(simplefin::lastDownload(s).isValid());

        simplefin::disconnect(s);
        QVERIFY(!simplefin::isConnected(s));
    }

    void sharedFolderSync()
    {
        Store &s = store();
        const QString mainDir = s.dataDir();
        QTemporaryDir shared, dirA, dirB, dirC;
        QString err;
        auto find = [&](const QString &desc) -> const Transaction * {
            for (const auto &t : s.transactions())
                if (t.description == desc) return &t;
            return nullptr;
        };
        auto addTx = [&](const QString &desc, qint64 amount, const QString &cat, const QVector<Receipt> &rec = {}) {
            Transaction t;
            t.categoryId = catId(cat);
            t.amount = amount;
            t.date = QDate(2026, 11, 10);
            t.description = desc;
            QVERIFY2(s.saveTransaction(t, rec, {}, &err), qPrintable(err));
        };

        // ---- Computer A creates the shared folder ----
        QVERIFY(s.open(dirA.path(), &err));
        const QString deviceA = s.deviceId();
        Receipt rc;
        rc.name = QStringLiteral("rent.pdf");
        rc.mime = QStringLiteral("application/pdf");
        rc.data = QByteArray("%PDF-1.4 fake receipt");
        addTx(QStringLiteral("Rent A"), 180000, QStringLiteral("Rent & Lease"), {rc});
        addTx(QStringLiteral("Paper A"), 2500, QStringLiteral("Office Supplies"));
        Settings st = s.settings();
        st.businessName = QStringLiteral("Shared Biz");
        s.saveSettings(st);
        QVERIFY(!sync::inspect(shared.path()).hasLedger);
        QVERIFY2(sync::connect(s, shared.path(), sync::JoinMode::Create, &err), qPrintable(err));
        QVERIFY(sync::isConnected(s));
        QVERIFY(QFile::exists(shared.filePath(QStringLiteral("ledgerly-shared.json"))));
        QCOMPARE(s.pendingChangeCount(), 0);
        QVERIFY(QDir(shared.filePath(QStringLiteral("changes/") + deviceA)).entryList({QStringLiteral("*.json")}).size() >= 1);

        // ---- Computer B joins, replacing its own (default) data ----
        QVERIFY(s.open(dirB.path(), &err));
        QVERIFY(s.deviceId() != deviceA);
        addTx(QStringLiteral("Local B"), 999, QStringLiteral("Travel"));
        const sync::FolderInfo info = sync::inspect(shared.path());
        QVERIFY(info.hasLedger);
        QVERIFY2(sync::connect(s, shared.path(), sync::JoinMode::Replace, &err), qPrintable(err));
        QVERIFY(!find(QStringLiteral("Local B")));
        QVERIFY(find(QStringLiteral("Rent A")));
        QCOMPARE(s.settings().businessName, QStringLiteral("Shared Biz"));
        QCOMPARE(s.categories().size(), defaults::categories().size()); // same default ids -> no duplicates
        const auto receipts = s.receiptsFor(find(QStringLiteral("Rent A"))->id);
        QCOMPARE(receipts.size(), 1);
        QCOMPARE(receipts[0].data, QByteArray("%PDF-1.4 fake receipt"));

        // B edits, adds and deletes
        Transaction rent = *find(QStringLiteral("Rent A"));
        rent.amount = 190000;
        QVERIFY(s.saveTransaction(rent, {}, {}, &err));
        addTx(QStringLiteral("From B"), 4200, QStringLiteral("Utilities"));
        QVERIFY(s.deleteTransactions({find(QStringLiteral("Paper A"))->id}, &err));
        Recurring net;
        net.categoryId = catId(QStringLiteral("Telephone & Internet"));
        net.amount = 8900;
        net.description = QStringLiteral("Internet");
        net.startDate = QDate::currentDate().addMonths(-2);
        QVERIFY(s.saveRecurring(net, &err));
        const int netPosted = s.recurringPostedCount(s.recurring().first().id);
        QVERIFY(netPosted >= 3);
        sync::Result r = sync::syncNow(s);
        QVERIFY2(r.ok, qPrintable(r.error));
        QVERIFY(r.exported > 0);

        // ---- Back on A: B's changes arrive ----
        QVERIFY(s.open(dirA.path(), &err));
        QCOMPARE(s.deviceId(), deviceA);
        r = sync::syncNow(s);
        QVERIFY2(r.ok, qPrintable(r.error));
        QVERIFY(r.imported > 0);
        QCOMPARE(find(QStringLiteral("Rent A"))->amount, 190000);
        QVERIFY(find(QStringLiteral("From B")));
        QVERIFY(!find(QStringLiteral("Paper A")));
        QCOMPARE(s.recurring().size(), 1);
        const QString ruleId = s.recurring().first().id;
        QCOMPARE(s.recurringPostedCount(ruleId), netPosted); // A didn't post the same payments again
        QVERIFY(sync::computers(s).size() == 1);

        // Both computers record the next payment independently -> still one transaction
        const QDate future = s.recurringRule(ruleId)->nextDate;
        QCOMPARE(s.processRecurring(future).size(), 1);
        rent = *find(QStringLiteral("Rent A"));
        rent.description = QStringLiteral("Rent A (edited on A)");
        QVERIFY(s.saveTransaction(rent, {}, {}, &err));
        QVERIFY(sync::syncNow(s).ok);

        QVERIFY(s.open(dirB.path(), &err));
        QCOMPARE(s.processRecurring(future).size(), 1); // B hasn't seen A's yet
        QTest::qWait(5);
        rent = *find(QStringLiteral("Rent A"));
        rent.description = QStringLiteral("Rent A (edited on B, later)");
        QVERIFY(s.saveTransaction(rent, {}, {}, &err));
        QVERIFY(sync::syncNow(s).ok);
        QCOMPARE(s.recurringPostedCount(ruleId), netPosted + 1);
        QVERIFY(find(QStringLiteral("Rent A (edited on B, later)")));

        QVERIFY(s.open(dirA.path(), &err));
        QVERIFY(sync::syncNow(s).ok);
        QCOMPARE(s.recurringPostedCount(ruleId), netPosted + 1);
        QVERIFY(find(QStringLiteral("Rent A (edited on B, later)"))); // most recent edit wins
        QVERIFY(!find(QStringLiteral("Rent A (edited on A)")));

        // ---- Computer C merges its own data in ----
        QVERIFY(s.open(dirC.path(), &err));
        addTx(QStringLiteral("C's own sale"), 50000, QStringLiteral("Sales Revenue"));
        QVERIFY2(sync::connect(s, shared.path(), sync::JoinMode::Merge, &err), qPrintable(err));
        QVERIFY(find(QStringLiteral("C's own sale")));
        QVERIFY(find(QStringLiteral("From B")));
        QCOMPARE(s.categories().size(), defaults::categories().size());
        QVERIFY(s.open(dirA.path(), &err));
        QVERIFY(sync::syncNow(s).ok);
        QVERIFY(find(QStringLiteral("C's own sale")));

        // Disconnecting stops tracking; the local data stays
        sync::disconnect(s);
        QVERIFY(!sync::isConnected(s));
        addTx(QStringLiteral("After disconnect"), 100, QStringLiteral("Travel"));
        QCOMPARE(s.pendingChangeCount(), 0);

        QVERIFY(s.open(mainDir, &err));
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
