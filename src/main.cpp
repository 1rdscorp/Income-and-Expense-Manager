#include "core/BankImport.h"
#include "core/Store.h"
#include "core/Sync.h"
#include "ui/MainWindow.h"
#include "ui/Theme.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QMessageBox>
#include <QStandardPaths>
#include <QTimer>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Ledgerly"));
    QApplication::setApplicationDisplayName(QStringLiteral("Ledgerly"));
    QApplication::setApplicationVersion(QStringLiteral(LEDGERLY_VERSION));
    QApplication::setDesktopFileName(QStringLiteral("ledgerly"));
    QApplication::setWindowIcon(Theme::appIcon());

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Income & Expense Manager for small businesses"));
    parser.addHelpOption();
    parser.addVersionOption();
    QCommandLineOption dataOpt(QStringLiteral("data-dir"), QStringLiteral("Use <dir> to store data instead of the default location."), QStringLiteral("dir"));
    QCommandLineOption shotOpt(QStringLiteral("screenshots"), QStringLiteral("Save a screenshot of every page to <dir> and exit (for testing)."), QStringLiteral("dir"));
    parser.addOption(dataOpt);
    parser.addOption(shotOpt);
    parser.process(app);

    QString dataDir = parser.value(dataOpt);
    if (dataDir.isEmpty()) dataDir = qEnvironmentVariable("LEDGERLY_DATA_DIR");
    if (dataDir.isEmpty()) dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);

    QString err;
    if (!Store::instance().open(dataDir, &err)) {
        QMessageBox::critical(nullptr, QStringLiteral("Ledgerly"), QStringLiteral("Could not open the database in\n%1\n\n%2").arg(QDir::toNativeSeparators(dataDir), err));
        return 1;
    }
    Theme::instance().apply(Store::instance().settings().theme);
    // Get other computers' changes first (shared folder), then record recurring payments
    // that fell due while the app was closed.
    if (sync::isConnected(Store::instance())) sync::syncNow(Store::instance());
    else Store::instance().processRecurring();

    MainWindow w;
    w.show();

    if (parser.isSet(shotOpt)) {
        const QString dir = parser.value(shotOpt);
        QDir().mkpath(dir);
        QTimer::singleShot(400, &w, [&w, dir]() {
            const char *names[] = {"dashboard", "transactions", "categories", "recurring", "bank", "reports", "backup", "settings"};
            for (int i = 0; i < MainWindow::PageCount; ++i) {
                w.showPage(i);
                QApplication::processEvents();
                QApplication::processEvents();
                w.grab().save(QDir(dir).filePath(QString::fromLatin1(names[i]) + QStringLiteral(".png")));
            }
            QApplication::quit();
        });
    }
    return app.exec();
}
