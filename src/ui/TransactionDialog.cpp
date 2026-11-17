#include "TransactionDialog.h"

#include "ReceiptViewer.h"
#include "RecurringDialog.h"
#include "Theme.h"
#include "Widgets.h"
#include "core/Defaults.h"
#include "core/Recurrence.h"
#include "core/Store.h"
#include "core/Util.h"

#include <QBuffer>
#include <QCheckBox>
#include <QComboBox>
#include <QCompleter>
#include <QDateEdit>
#include <QDragEnterEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QImage>
#include <QImageReader>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QMimeData>
#include <QMimeDatabase>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QShortcut>
#include <QStandardPaths>
#include <QVBoxLayout>
#include <algorithm>
#include <functional>

namespace {

constexpr qint64 kMaxFile = 25LL * 1024 * 1024;
TxType g_lastType = TxType::Expense;
QHash<int, QString> g_lastCategory; // per type, remembered for quick daily entry
QString g_lastDir;

QLabel *fieldLabel(const QString &text)
{
    auto *l = new QLabel(text);
    l->setStyleSheet(QStringLiteral("font-weight:600;"));
    return l;
}

/* Shrinks big photos to max 2000px JPEG; returns original data when not beneficial. */
QByteArray maybeCompress(const QByteArray &data, const QString &mime, QString *outMime, QString *outName)
{
    static const QStringList compressible = {QStringLiteral("image/jpeg"), QStringLiteral("image/png"), QStringLiteral("image/bmp"), QStringLiteral("image/webp")};
    if (!Store::instance().settings().compressReceipts || !compressible.contains(mime) || data.size() < 700 * 1024) return data;
    QBuffer in;
    in.setData(data);
    QImageReader reader(&in);
    reader.setAutoTransform(true); // honour EXIF rotation from phone cameras
    QImage img = reader.read();
    if (img.isNull()) return data;
    if (std::max(img.width(), img.height()) > 2000) img = img.scaled(2000, 2000, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    if (img.hasAlphaChannel()) {
        QImage flat(img.size(), QImage::Format_RGB32);
        flat.fill(Qt::white);
        QPainter p(&flat);
        p.drawImage(0, 0, img);
        p.end();
        img = flat;
    }
    QByteArray out;
    QBuffer buf(&out);
    buf.open(QIODevice::WriteOnly);
    if (!img.save(&buf, "JPEG", 82) || out.size() >= data.size()) return data;
    *outMime = QStringLiteral("image/jpeg");
    *outName = QFileInfo(*outName).completeBaseName() + QStringLiteral(".jpg");
    return out;
}

} // namespace

TransactionDialog::TransactionDialog(QWidget *parent)
    : QDialog(parent)
{
    setMinimumWidth(640);
    setAcceptDrops(true);
    auto *root = new QVBoxLayout(this);
    root->setSpacing(10);

    m_typeSeg = new SegmentedControl({{QStringLiteral("expense"), QStringLiteral("   Expense   ")}, {QStringLiteral("income"), QStringLiteral("   Income   ")}});
    root->addWidget(m_typeSeg, 0, Qt::AlignLeft);

    auto *g = new QGridLayout;
    g->setHorizontalSpacing(14);
    g->setVerticalSpacing(4);
    m_amount = new QLineEdit;
    m_amount->setObjectName(QStringLiteral("amountEdit"));
    m_amount->setPlaceholderText(QStringLiteral("0.00"));
    m_date = new QDateEdit(QDate::currentDate());
    m_date->setCalendarPopup(true);
    m_date->setObjectName(QStringLiteral("dateEdit"));
    m_date->setDisplayFormat(QStringLiteral("ddd, d MMM yyyy"));
    m_date->setMinimumHeight(m_amount->sizeHint().height());
    g->addWidget(fieldLabel(QStringLiteral("Amount (%1)").arg(util::currencyCode())), 0, 0);
    g->addWidget(fieldLabel(QStringLiteral("Date")), 0, 1);
    g->addWidget(m_amount, 1, 0);
    g->addWidget(m_date, 1, 1);
    g->addItem(new QSpacerItem(1, 8), 2, 0);

    m_category = new QComboBox;
    m_category->setMaxVisibleItems(16);
    m_category->setObjectName(QStringLiteral("categoryCombo"));
    g->addWidget(fieldLabel(QStringLiteral("Category")), 3, 0, 1, 2);
    g->addWidget(m_category, 4, 0, 1, 2);
    g->addItem(new QSpacerItem(1, 8), 5, 0);

    m_desc = new QLineEdit;
    m_desc->setMaxLength(200);
    m_desc->setObjectName(QStringLiteral("descEdit"));
    m_desc->setPlaceholderText(QStringLiteral("e.g. Printer paper, Invoice #1042"));
    g->addWidget(fieldLabel(QStringLiteral("Description")), 6, 0, 1, 2);
    g->addWidget(m_desc, 7, 0, 1, 2);
    g->addItem(new QSpacerItem(1, 8), 8, 0);

    m_vendor = new QLineEdit;
    m_vendor->setMaxLength(120);
    m_vendor->setObjectName(QStringLiteral("vendorEdit"));
    auto *comp = new QCompleter(Store::instance().knownVendors(), m_vendor);
    comp->setCaseSensitivity(Qt::CaseInsensitive);
    comp->setFilterMode(Qt::MatchContains);
    m_vendor->setCompleter(comp);
    m_payment = new QComboBox;
    m_payment->addItem(QStringLiteral("—"), QString());
    m_payment->setObjectName(QStringLiteral("paymentCombo"));
    for (const auto &p : defaults::paymentMethods()) m_payment->addItem(p, p);
    m_vendorLabel = fieldLabel(QStringLiteral("Vendor / payee"));
    g->addWidget(m_vendorLabel, 9, 0);
    g->addWidget(fieldLabel(QStringLiteral("Payment method")), 9, 1);
    g->addWidget(m_vendor, 10, 0);
    g->addWidget(m_payment, 10, 1);
    g->addItem(new QSpacerItem(1, 8), 11, 0);

    m_notes = new QPlainTextEdit;
    m_notes->setFixedHeight(60);
    m_notes->setObjectName(QStringLiteral("notesEdit"));
    m_notes->setTabChangesFocus(true);
    g->addWidget(fieldLabel(QStringLiteral("Notes")), 12, 0, 1, 2);
    g->addWidget(m_notes, 13, 0, 1, 2);
    g->setColumnStretch(0, 1);
    g->setColumnStretch(1, 1);
    root->addLayout(g);

    // Repeat: makes this transaction the first payment of a recurring series
    m_repeatRow = new QWidget;
    auto *rv = new QVBoxLayout(m_repeatRow);
    rv->setContentsMargins(0, 0, 0, 0);
    rv->setSpacing(4);
    auto *rr = new QHBoxLayout;
    m_repeat = new QCheckBox(QStringLiteral("Repeat this transaction"));
    m_repeat->setObjectName(QStringLiteral("repeatCheck"));
    m_repeat->setIcon(Theme::icon(QStringLiteral("repeat"), Theme::c().accent));
    m_repeatFreq = new QComboBox;
    m_repeatFreq->setObjectName(QStringLiteral("repeatFreq"));
    for (Frequency f : {Frequency::Monthly, Frequency::Weekly, Frequency::Quarterly, Frequency::Yearly})
        m_repeatFreq->addItem(frequencyLabel(f), frequencyToString(f));
    m_repeatFreq->setEnabled(false);
    m_repeatHint = ui::muted(QString());
    rr->addWidget(m_repeat);
    rr->addWidget(m_repeatFreq);
    rr->addWidget(m_repeatHint, 1);
    rv->addLayout(rr);
    m_recordMissed = new QCheckBox;
    m_recordMissed->setObjectName(QStringLiteral("recordMissed"));
    m_recordMissed->hide();
    rv->addWidget(m_recordMissed);
    root->addWidget(m_repeatRow);

    // Series: shown when the transaction already belongs to a recurring rule
    m_seriesRow = new QFrame;
    m_seriesRow->setObjectName(QStringLiteral("infoBanner"));
    auto *sr = new QHBoxLayout(m_seriesRow);
    sr->setContentsMargins(12, 8, 12, 8);
    auto *seriesIcon = new QLabel;
    seriesIcon->setPixmap(Theme::iconPixmap(QStringLiteral("repeat"), Theme::c().accent, 18));
    m_seriesNote = new QLabel;
    m_seriesNote->setWordWrap(true);
    m_seriesNote->setTextFormat(Qt::RichText);
    auto *editSeries = ui::button(QStringLiteral("Edit series…"), QStringLiteral("edit"));
    auto *stopSeries = ui::button(QStringLiteral("Stop repeating"), QStringLiteral("x"), QStringLiteral("dangerText"));
    stopSeries->setAccessibleName(QStringLiteral("stopRepeating"));
    sr->addWidget(seriesIcon, 0, Qt::AlignTop);
    sr->addWidget(m_seriesNote, 1);
    sr->addWidget(editSeries);
    sr->addWidget(stopSeries);
    m_seriesRow->hide();
    root->addWidget(m_seriesRow);

    connect(m_repeat, &QCheckBox::toggled, this, &TransactionDialog::syncRepeat);
    connect(m_repeatFreq, &QComboBox::currentIndexChanged, this, &TransactionDialog::syncRepeat);
    connect(m_date, &QDateEdit::dateChanged, this, &TransactionDialog::syncRepeat);
    connect(m_recordMissed, &QCheckBox::toggled, this, &TransactionDialog::syncRepeat);
    connect(editSeries, &QPushButton::clicked, this, [this]() {
        const Recurring *rule = Store::instance().recurringRule(m_tx.recurringId);
        if (!rule) return;
        RecurringDialog dlg(this);
        dlg.setRule(*rule);
        dlg.exec();
        syncSeries();
    });
    connect(stopSeries, &QPushButton::clicked, this, &TransactionDialog::stopRepeating);

    // Receipts
    auto *rc = new QFrame;
    rc->setObjectName(QStringLiteral("card"));
    auto *rl = new QVBoxLayout(rc);
    rl->setContentsMargins(12, 10, 12, 10);
    auto *rh = new QHBoxLayout;
    rh->addWidget(fieldLabel(QStringLiteral("Receipts & attachments")));
    rh->addWidget(ui::muted(QStringLiteral("or drag & drop files onto this window")), 1);
    auto *addBtn = ui::button(QStringLiteral("Attach files…"), QStringLiteral("clip"));
    m_viewBtn = ui::button(QStringLiteral("View"), QStringLiteral("eye"));
    m_removeBtn = ui::button(QStringLiteral("Remove"), QStringLiteral("x"));
    rh->addWidget(addBtn);
    rh->addWidget(m_viewBtn);
    rh->addWidget(m_removeBtn);
    rl->addLayout(rh);
    m_receiptList = new QListWidget;
    m_receiptList->setObjectName(QStringLiteral("plainList"));
    m_receiptList->setViewMode(QListView::IconMode);
    m_receiptList->setIconSize(QSize(88, 88));
    m_receiptList->setGridSize(QSize(124, 128));
    m_receiptList->setResizeMode(QListView::Adjust);
    m_receiptList->setMovement(QListView::Static);
    m_receiptList->setWordWrap(true);
    m_receiptList->setFixedHeight(140);
    m_receiptList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    rl->addWidget(m_receiptList);
    root->addWidget(rc);

    // Buttons
    auto *bar = new QHBoxLayout;
    m_deleteBtn = ui::button(QStringLiteral("Delete"), QStringLiteral("trash"), QStringLiteral("dangerText"));
    auto *cancel = ui::button(QStringLiteral("Cancel"));
    m_anotherBtn = ui::button(QStringLiteral("Save && add another"));
    m_saveBtn = ui::button(QStringLiteral("Save"), QString(), QStringLiteral("primary"));
    m_saveBtn->setDefault(true);
    m_saveBtn->setObjectName(QStringLiteral("primary"));
    m_saveBtn->setAccessibleName(QStringLiteral("save"));
    m_anotherBtn->setAccessibleName(QStringLiteral("saveAnother"));
    m_deleteBtn->setAccessibleName(QStringLiteral("delete"));
    m_receiptList->setAccessibleName(QStringLiteral("receipts"));
    bar->addWidget(m_deleteBtn);
    bar->addStretch(1);
    bar->addWidget(cancel);
    bar->addWidget(m_anotherBtn);
    bar->addWidget(m_saveBtn);
    root->addLayout(bar);

    connect(m_typeSeg, &SegmentedControl::changed, this, [this]() { syncType(); });
    connect(addBtn, &QPushButton::clicked, this, [this]() {
        if (g_lastDir.isEmpty()) g_lastDir = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
        const QStringList files = QFileDialog::getOpenFileNames(this, QStringLiteral("Attach receipts"), g_lastDir,
                                                                QStringLiteral("Images and PDFs (*.png *.jpg *.jpeg *.gif *.bmp *.webp *.pdf);;All files (*)"));
        if (!files.isEmpty()) g_lastDir = QFileInfo(files.first()).absolutePath();
        addFiles(files);
    });
    auto viewSelected = [this]() {
        auto *it = m_receiptList->currentItem();
        if (!it) return;
        ReceiptViewer::display(this, m_receipts.value(it->data(Qt::UserRole).toInt()).r);
    };
    connect(m_viewBtn, &QPushButton::clicked, this, viewSelected);
    connect(m_receiptList, &QListWidget::itemDoubleClicked, this, viewSelected);
    connect(m_removeBtn, &QPushButton::clicked, this, [this]() {
        QList<int> rows;
        for (auto *it : m_receiptList->selectedItems()) rows << it->data(Qt::UserRole).toInt();
        std::sort(rows.begin(), rows.end(), std::greater<int>());
        for (int r : rows) {
            if (!m_receipts[r].isNew) m_removed << m_receipts[r].r.id;
            m_receipts.removeAt(r);
        }
        renderReceipts();
    });
    connect(m_receiptList, &QListWidget::itemSelectionChanged, this, [this]() {
        const bool any = !m_receiptList->selectedItems().isEmpty();
        m_viewBtn->setEnabled(any);
        m_removeBtn->setEnabled(any);
    });
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_saveBtn, &QPushButton::clicked, this, [this]() { save(false); });
    connect(m_anotherBtn, &QPushButton::clicked, this, [this]() { save(true); });
    connect(m_deleteBtn, &QPushButton::clicked, this, &TransactionDialog::remove);
    connect(new QShortcut(QKeySequence(QStringLiteral("Ctrl+Return")), this), &QShortcut::activated, this, [this]() { save(false); });

    setNewType(g_lastType);
}

void TransactionDialog::setNewType(TxType type)
{
    setWindowTitle(QStringLiteral("New transaction"));
    m_editing = false;
    m_tx = Transaction();
    m_tx.type = type;
    m_tx.date = QDate::currentDate();
    m_typeSeg->setValue(typeToString(type));
    m_type = type;
    m_deleteBtn->hide();
    m_anotherBtn->show();
    m_repeat->setChecked(false);
    m_recordMissed->setChecked(true); // a new transaction dated in the past: record what came after it
    syncSeries();
    m_receipts.clear();
    m_removed.clear();
    loadFields(m_tx);
    renderReceipts();
    m_amount->setFocus();
}

void TransactionDialog::setTransaction(const Transaction &t)
{
    setWindowTitle(QStringLiteral("Edit transaction"));
    m_editing = true;
    m_tx = t;
    m_type = t.type;
    m_typeSeg->setValue(typeToString(t.type));
    m_deleteBtn->show();
    m_anotherBtn->hide();
    m_repeat->setChecked(false);
    m_recordMissed->setChecked(false); // existing payments may already be recorded by hand
    syncSeries();
    m_saveBtn->setText(QStringLiteral("Save changes"));
    m_receipts.clear();
    m_removed.clear();
    for (const auto &r : Store::instance().receiptsFor(t.id)) m_receipts.push_back({r, false});
    loadFields(t);
    renderReceipts();
    m_amount->setFocus();
    m_amount->selectAll();
}

void TransactionDialog::setTemplate(const Transaction &t)
{
    setNewType(t.type);
    setWindowTitle(QStringLiteral("New transaction (copy)"));
    Transaction copy = t;
    copy.id.clear();
    copy.createdAt.clear();
    copy.date = QDate::currentDate();
    m_tx = copy;
    loadFields(copy);
    m_amount->setFocus();
    m_amount->selectAll();
}

void TransactionDialog::loadFields(const Transaction &t)
{
    m_amount->setText(t.amount ? util::amountPlain(t.amount) : QString());
    m_date->setDate(t.date.isValid() ? t.date : QDate::currentDate());
    m_desc->setText(t.description);
    m_vendor->setText(t.vendor);
    const int pi = m_payment->findData(t.paymentMethod);
    if (pi >= 0) m_payment->setCurrentIndex(pi);
    else if (!t.paymentMethod.isEmpty()) {
        m_payment->addItem(t.paymentMethod, t.paymentMethod);
        m_payment->setCurrentIndex(m_payment->count() - 1);
    } else m_payment->setCurrentIndex(0);
    m_notes->setPlainText(t.notes);
    syncType();
}

void TransactionDialog::fillCategories()
{
    const QString keep = m_category->currentData().toString();
    m_category->clear();
    const auto list = Store::instance().categoriesOfType(m_type);
    if (list.isEmpty()) {
        m_category->addItem(QStringLiteral("No %1 categories — add one on the Categories page").arg(typeToString(m_type)), QString());
        return;
    }
    m_category->addItem(QStringLiteral("Select a category…"), QString());
    for (const auto &c : list) m_category->addItem(c.icon + QStringLiteral("   ") + c.name, c.id);
    QString want = m_tx.categoryId;
    if (Store::instance().category(want) == nullptr || Store::instance().category(want)->type != m_type) want = keep;
    if (want.isEmpty() || m_category->findData(want) < 0) want = g_lastCategory.value(static_cast<int>(m_type));
    const int idx = m_category->findData(want);
    m_category->setCurrentIndex(idx > 0 ? idx : 0);
}

void TransactionDialog::syncType()
{
    m_type = typeFromString(m_typeSeg->value());
    m_vendorLabel->setText(m_type == TxType::Income ? QStringLiteral("Customer / payer") : QStringLiteral("Vendor / payee"));
    fillCategories();
}

void TransactionDialog::renderReceipts()
{
    m_receiptList->clear();
    const auto &th = Theme::c();
    for (int i = 0; i < m_receipts.size(); ++i) {
        const Receipt &r = m_receipts[i].r;
        // Framed tile so white paper receipts stay visible
        QPixmap thumb(88, 88);
        thumb.fill(th.surface3);
        {
            QPainter p(&thumb);
            QImage img;
            if (r.isImage() && img.loadFromData(r.data)) {
                const QImage sc = img.scaled(80, 80, Qt::KeepAspectRatio, Qt::SmoothTransformation);
                const QPoint at((88 - sc.width()) / 2, (88 - sc.height()) / 2);
                p.drawImage(at, sc);
                p.setPen(th.borderStrong);
                p.drawRect(QRect(at, sc.size()).adjusted(0, 0, -1, -1));
            } else {
                const QPixmap ic = Theme::iconPixmap(r.mime == QLatin1String("application/pdf") ? QStringLiteral("pdf") : QStringLiteral("file"), th.muted, 48);
                p.drawPixmap((88 - 48) / 2, (88 - 48) / 2, ic);
            }
        }
        auto *it = new QListWidgetItem(QIcon(thumb), QStringLiteral("%1%2\n%3").arg(m_receipts[i].isNew ? QStringLiteral("NEW · ") : QString(), r.name, util::fmtBytes(r.data.size())));
        it->setData(Qt::UserRole, i);
        it->setToolTip(r.name);
        m_receiptList->addItem(it);
    }
    if (m_receipts.isEmpty()) {
        auto *it = new QListWidgetItem(QStringLiteral("No receipts attached"));
        it->setFlags(Qt::NoItemFlags);
        m_receiptList->addItem(it);
    }
    m_viewBtn->setEnabled(false);
    m_removeBtn->setEnabled(false);
}

int TransactionDialog::addFiles(const QStringList &paths)
{
    QMimeDatabase mdb;
    int added = 0;
    QStringList problems;
    for (const QString &p : paths) {
        QFileInfo fi(p);
        if (!fi.isFile()) continue;
        if (fi.size() > kMaxFile) {
            problems << QStringLiteral("%1 is larger than 25 MB").arg(fi.fileName());
            continue;
        }
        QFile f(p);
        if (!f.open(QIODevice::ReadOnly)) {
            problems << QStringLiteral("%1: %2").arg(fi.fileName(), f.errorString());
            continue;
        }
        Receipt r;
        r.id = util::newId();
        r.name = fi.fileName();
        r.mime = mdb.mimeTypeForFile(fi).name();
        r.createdAt = util::nowIso();
        r.data = maybeCompress(f.readAll(), r.mime, &r.mime, &r.name);
        r.size = r.data.size();
        m_receipts.push_back({r, true});
        ++added;
    }
    if (!problems.isEmpty()) QMessageBox::warning(this, QStringLiteral("Some files were not attached"), problems.join(QLatin1Char('\n')));
    renderReceipts();
    return added;
}

void TransactionDialog::dragEnterEvent(QDragEnterEvent *e)
{
    if (e->mimeData()->hasUrls()) e->acceptProposedAction();
}

void TransactionDialog::dropEvent(QDropEvent *e)
{
    QStringList paths;
    for (const QUrl &u : e->mimeData()->urls())
        if (u.isLocalFile()) paths << u.toLocalFile();
    addFiles(paths);
    e->acceptProposedAction();
}

void TransactionDialog::save(bool another)
{
    Transaction t = m_tx;
    t.amount = util::parseAmount(m_amount->text());
    if (t.amount <= 0) {
        QMessageBox::warning(this, windowTitle(), QStringLiteral("Enter an amount greater than zero."));
        m_amount->setFocus();
        m_amount->selectAll();
        return;
    }
    t.categoryId = m_category->currentData().toString();
    if (t.categoryId.isEmpty()) {
        QMessageBox::warning(this, windowTitle(), QStringLiteral("Choose a category."));
        m_category->setFocus();
        m_category->showPopup();
        return;
    }
    t.type = m_type;
    t.date = m_date->date();
    t.description = m_desc->text().trimmed();
    t.vendor = m_vendor->text().trimmed();
    t.paymentMethod = m_payment->currentData().toString();
    t.notes = m_notes->toPlainText().trimmed();

    QVector<Receipt> fresh;
    for (const auto &p : m_receipts)
        if (p.isNew) fresh << p.r;
    QString err;
    const bool repeat = m_repeat->isChecked() && !Store::instance().recurringRule(m_tx.recurringId);
    if (t.id.isEmpty()) t.id = util::newId();
    if (!Store::instance().saveTransaction(t, fresh, m_removed, &err)) {
        QMessageBox::warning(this, windowTitle(), err);
        return;
    }
    if (repeat) {
        // This transaction becomes the first payment of a new recurring series.
        if (!Store::instance().makeRecurring(t.id, frequencyFromString(m_repeatFreq->currentData().toString()), m_recordMissed->isChecked(), &err))
            QMessageBox::warning(this, windowTitle(), QStringLiteral("The transaction was saved, but the repeat could not be set up: %1").arg(err));
    }
    g_lastType = t.type;
    g_lastCategory.insert(static_cast<int>(t.type), t.categoryId);
    m_type = t.type;
    m_outcome = another ? SavedAddAnother : Saved;
    accept();
}

QVector<QDate> TransactionDialog::missedDates() const
{
    Recurring r;
    r.frequency = frequencyFromString(m_repeatFreq->currentData().toString());
    r.startDate = m_date->date();
    r.day = r.startDate.day();
    QVector<QDate> out;
    const QDate today = QDate::currentDate();
    QDate d = recurrence::advance(r.startDate, r.frequency, r.day);
    while (d.isValid() && d <= today && out.size() < 1000) {
        out << d;
        d = recurrence::advance(d, r.frequency, r.day);
    }
    return out;
}

void TransactionDialog::syncRepeat()
{
    const bool on = m_repeat->isChecked();
    m_repeatFreq->setEnabled(on);
    if (!on) {
        m_repeatHint->setText(m_editing ? QStringLiteral("make this a regular bill or income, e.g. rent or internet")
                                        : QStringLiteral("e.g. rent, internet, subscriptions"));
        m_recordMissed->hide();
        return;
    }
    Recurring r;
    r.frequency = frequencyFromString(m_repeatFreq->currentData().toString());
    r.startDate = m_date->date();
    r.day = r.startDate.day();
    const QVector<QDate> missed = missedDates();
    QDate next = missed.isEmpty() ? recurrence::advance(r.startDate, r.frequency, r.day) : recurrence::advance(missed.last(), r.frequency, r.day);
    m_repeatHint->setText(QStringLiteral("%1 — next on %2, recorded automatically").arg(recurrence::describe(r), util::fmtDate(next)));
    if (missed.isEmpty()) {
        m_recordMissed->hide();
    } else {
        QStringList dates;
        for (int i = 0; i < std::min<int>(3, missed.size()); ++i) dates << util::fmtDate(missed[i]);
        if (missed.size() > 3) dates << QStringLiteral("…");
        m_recordMissed->setText(QStringLiteral("Also record the %1 payment%2 since then (%3)")
                                    .arg(missed.size())
                                    .arg(missed.size() == 1 ? QString() : QStringLiteral("s"), dates.join(QStringLiteral(", "))));
        m_recordMissed->show();
    }
}

void TransactionDialog::syncSeries()
{
    const Recurring *rule = Store::instance().recurringRule(m_tx.recurringId);
    if (rule) {
        const int n = Store::instance().recurringPostedCount(rule->id);
        m_seriesNote->setText(QStringLiteral("<b>Part of the recurring “%1”</b> — %2, %3 payment%4 recorded%5.<br>Changes here apply to this payment only.")
                                  .arg(rule->title().toHtmlEscaped(), recurrence::describe(*rule).toLower().toHtmlEscaped())
                                  .arg(n)
                                  .arg(n == 1 ? QString() : QStringLiteral("s"),
                                       rule->active && !rule->finished() ? QStringLiteral(", next on %1").arg(util::fmtDate(rule->nextDate))
                                                                         : (rule->active ? QStringLiteral(", ended") : QStringLiteral(", paused"))));
    }
    m_seriesRow->setVisible(rule != nullptr);
    m_repeatRow->setVisible(rule == nullptr);
    syncRepeat();
}

void TransactionDialog::setRepeatChecked(bool on)
{
    if (!Store::instance().recurringRule(m_tx.recurringId)) m_repeat->setChecked(on);
}

void TransactionDialog::stopRepeating()
{
    const Recurring *rule = Store::instance().recurringRule(m_tx.recurringId);
    if (!rule) return;
    const int n = Store::instance().recurringPostedCount(rule->id);
    if (QMessageBox::question(this, QStringLiteral("Stop repeating"),
                              QStringLiteral("Stop the recurring “%1”?\n\nNo more payments will be recorded automatically. "
                                             "The %2 payment(s) already recorded, including this one, are kept.")
                                  .arg(rule->title())
                                  .arg(n))
        != QMessageBox::Yes)
        return;
    QString err;
    if (!Store::instance().deleteRecurring(rule->id, &err)) {
        QMessageBox::warning(this, windowTitle(), err);
        return;
    }
    m_tx.recurringId.clear();
    syncSeries();
}

void TransactionDialog::remove()
{
    if (!m_editing) return;
    const int n = m_receipts.size();
    const QString msg = QStringLiteral("Delete this %1 of %2%3?\n\nThis can't be undone.")
                            .arg(typeToString(m_tx.type), util::money(m_tx.amount), n ? QStringLiteral(" and its %1 receipt(s)").arg(n) : QString());
    if (QMessageBox::question(this, QStringLiteral("Delete transaction"), msg) != QMessageBox::Yes) return;
    QString err;
    if (!Store::instance().deleteTransactions({m_tx.id}, &err)) {
        QMessageBox::warning(this, windowTitle(), err);
        return;
    }
    m_outcome = Deleted;
    accept();
}
