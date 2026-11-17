#include "ImportDialog.h"

#include "BankDialogs.h"
#include "Theme.h"
#include "Widgets.h"
#include "core/Store.h"
#include "core/Util.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QRegularExpression>
#include <QMessageBox>
#include <QPushButton>
#include <QStandardPaths>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {
enum Col { ColDate, ColDesc, ColAmount, ColCategory, ColNote, ColCount };
const char *kNewAccount = "__new__";
QString lastDir;

QLabel *bold(const QString &t)
{
    auto *l = new QLabel(t);
    l->setStyleSheet(QStringLiteral("font-weight:600;"));
    return l;
}
} // namespace

ImportDialog::ImportDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("Import bank or card statement"));
    resize(1000, 700);
    auto *root = new QVBoxLayout(this);
    root->setSpacing(10);

    auto *top = new QGridLayout;
    top->setHorizontalSpacing(10);
    m_account = new QComboBox;
    m_account->setObjectName(QStringLiteral("accountCombo"));
    m_account->setMinimumWidth(260);
    m_file = new QLineEdit;
    m_file->setReadOnly(true);
    m_file->setPlaceholderText(QStringLiteral("Choose a statement file (OFX, QFX, QIF or CSV)…"));
    auto *browse = ui::button(QStringLiteral("Choose file…"), QStringLiteral("upload"));
    top->addWidget(bold(QStringLiteral("Account or card")), 0, 0);
    top->addWidget(bold(QStringLiteral("Statement file")), 0, 1);
    top->addWidget(m_account, 1, 0);
    auto *fileRow = new QHBoxLayout;
    fileRow->addWidget(m_file, 1);
    fileRow->addWidget(browse);
    top->addLayout(fileRow, 1, 1);
    top->setColumnStretch(1, 1);
    root->addLayout(top);

    m_info = ui::muted(QStringLiteral("Download a statement from your online banking — choose OFX, QFX, Quicken/QIF or CSV/Excel-CSV format."));
    m_info->setTextFormat(Qt::RichText);
    root->addWidget(m_info);

    // CSV column mapping
    m_csvBox = new QFrame;
    m_csvBox->setObjectName(QStringLiteral("card"));
    auto *cg = new QGridLayout(m_csvBox);
    cg->setContentsMargins(12, 10, 12, 10);
    cg->setHorizontalSpacing(10);
    m_cDate = new QComboBox;
    m_cDesc = new QComboBox;
    m_cAmount = new QComboBox;
    m_cDebit = new QComboBox;
    m_cCredit = new QComboBox;
    m_cFormat = new QComboBox;
    for (const QString &f : bank::dateFormats())
        m_cFormat->addItem(QStringLiteral("%1   (e.g. %2)").arg(f, QLocale::c().toString(QDate::currentDate(), f)), f);
    const QStringList labels = {QStringLiteral("Date column"), QStringLiteral("Description"), QStringLiteral("Amount (+/−)"),
                                QStringLiteral("…or Money out"), QStringLiteral("Money in"), QStringLiteral("Date format")};
    QComboBox *combos[] = {m_cDate, m_cDesc, m_cAmount, m_cDebit, m_cCredit, m_cFormat};
    for (int i = 0; i < 6; ++i) {
        cg->addWidget(bold(labels[i]), 0, i);
        cg->addWidget(combos[i], 1, i);
        connect(combos[i], &QComboBox::currentIndexChanged, this, [this]() {
            if (!m_updating) reparse(true);
        });
    }
    root->addWidget(m_csvBox);
    m_csvBox->hide();

    m_flip = new QCheckBox(QStringLiteral("This statement shows money going out as positive numbers (swap income and expenses)"));
    root->addWidget(m_flip);
    connect(m_flip, &QCheckBox::toggled, this, [this]() {
        if (!m_updating) prepareItems();
    });

    m_table = new QTableWidget(0, ColCount);
    m_table->setHorizontalHeaderLabels({QStringLiteral("Date"), QStringLiteral("Description"), QStringLiteral("Amount"), QStringLiteral("Category"),
                                        QStringLiteral("Status")});
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setShowGrid(false);
    m_table->verticalHeader()->setDefaultSectionSize(34);
    m_table->horizontalHeader()->setSectionResizeMode(ColDate, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(ColDesc, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(ColAmount, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(ColCategory, QHeaderView::Interactive);
    m_table->horizontalHeader()->setSectionResizeMode(ColNote, QHeaderView::Interactive);
    m_table->setColumnWidth(ColCategory, 230);
    m_table->setColumnWidth(ColNote, 230);
    for (int c = 0; c < ColCount; ++c)
        m_table->horizontalHeaderItem(c)->setTextAlignment((c == ColAmount ? Qt::AlignRight : Qt::AlignLeft) | Qt::AlignVCenter);
    root->addWidget(m_table, 1);
    connect(m_table, &QTableWidget::itemChanged, this, [this](QTableWidgetItem *it) {
        if (m_updating || it->column() != ColDate) return;
        const int r = it->row();
        if (r < m_items.size()) m_items[r].include = it->checkState() == Qt::Checked;
        updateSummary();
    });

    auto *bar = new QHBoxLayout;
    m_summary = new QLabel;
    m_summary->setTextFormat(Qt::RichText);
    m_ruleBtn = ui::button(QStringLiteral("Make rule from selected line…"), QStringLiteral("tag"));
    m_ruleBtn->setToolTip(QStringLiteral("Always use the chosen category for descriptions like the selected line"));
    auto *cancel = ui::button(QStringLiteral("Cancel"));
    m_importBtn = ui::button(QStringLiteral("Import"), QStringLiteral("download"), QStringLiteral("primary"));
    m_importBtn->setAccessibleName(QStringLiteral("import"));
    bar->addWidget(m_summary, 1);
    bar->addWidget(m_ruleBtn);
    bar->addWidget(cancel);
    bar->addWidget(m_importBtn);
    root->addLayout(bar);

    connect(browse, &QPushButton::clicked, this, &ImportDialog::chooseFile);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_importBtn, &QPushButton::clicked, this, &ImportDialog::doImport);
    connect(m_ruleBtn, &QPushButton::clicked, this, &ImportDialog::makeRuleFromSelection);
    connect(m_account, &QComboBox::activated, this, [this]() {
        if (m_account->currentData().toString() == QLatin1String(kNewAccount)) {
            AccountDialog dlg(this);
            const bool ok = dlg.exec() == QDialog::Accepted;
            fillAccounts(ok ? dlg.savedId() : QString());
        }
        const Account a = currentAccount();
        m_updating = true;
        m_flip->setChecked(a.flipSigns);
        m_updating = false;
        if (!m_data.isEmpty()) reparse(false);
    });

    fillAccounts(QString());
    updateSummary();
}

void ImportDialog::fillAccounts(const QString &select)
{
    const QString keep = select.isEmpty() ? m_account->currentData().toString() : select;
    m_account->clear();
    for (const auto &a : Store::instance().accounts())
        m_account->addItem(Theme::icon(a.isCard() ? QStringLiteral("card") : QStringLiteral("bank")), a.name, a.id);
    m_account->addItem(Theme::icon(QStringLiteral("plus")), QStringLiteral("New account or card…"), QString::fromLatin1(kNewAccount));
    int idx = m_account->findData(keep);
    if (idx < 0 || keep == QLatin1String(kNewAccount)) idx = 0;
    m_account->setCurrentIndex(idx);
}

void ImportDialog::setAccount(const QString &accountId)
{
    fillAccounts(accountId);
    m_updating = true;
    m_flip->setChecked(currentAccount().flipSigns);
    m_updating = false;
}

Account ImportDialog::currentAccount() const
{
    const Account *a = Store::instance().account(m_account->currentData().toString());
    return a ? *a : Account();
}

void ImportDialog::chooseFile()
{
    if (lastDir.isEmpty()) lastDir = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Choose a statement"), lastDir,
                                                      QStringLiteral("Statements (*.ofx *.qfx *.qif *.csv *.txt);;All files (*)"));
    if (path.isEmpty()) return;
    lastDir = QFileInfo(path).absolutePath();
    loadFile(path);
}

bool ImportDialog::loadFile(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, windowTitle(), QStringLiteral("Can't open the file: %1").arg(f.errorString()));
        return false;
    }
    m_path = path;
    m_data = f.readAll();
    m_file->setText(QDir::toNativeSeparators(path));
    reparse(false);
    return m_parse.ok();
}

void ImportDialog::fillMappingCombos()
{
    m_updating = true;
    QStringList header;
    const auto &rows = m_parse.csvRows;
    const int headerRow = m_parse.csvHeaderRow;
    int cols = 0;
    for (int r = 0; r < std::min<int>(20, rows.size()); ++r) cols = std::max(cols, static_cast<int>(rows[r].size()));
    for (int c = 0; c < cols; ++c) {
        QString name = headerRow >= 0 ? rows[headerRow].value(c) : QString();
        const QString sample = rows.value(headerRow + 1).value(c);
        header << (name.isEmpty() ? QStringLiteral("Column %1").arg(c + 1) : name) + (sample.isEmpty() ? QString() : QStringLiteral("  (e.g. %1)").arg(sample.left(24)));
    }
    auto fill = [&](QComboBox *cb, int sel, bool allowNone) {
        cb->clear();
        if (allowNone) cb->addItem(QStringLiteral("(none)"), -1);
        for (int c = 0; c < header.size(); ++c) cb->addItem(header[c], c);
        const int idx = cb->findData(sel);
        cb->setCurrentIndex(idx >= 0 ? idx : 0);
    };
    const auto &m = m_parse.csvMapping;
    fill(m_cDate, m.date, false);
    fill(m_cDesc, m.description, false);
    fill(m_cAmount, m.amount, true);
    fill(m_cDebit, m.debit, true);
    fill(m_cCredit, m.credit, true);
    const int fi = m_cFormat->findData(m.dateFormat);
    if (fi >= 0) m_cFormat->setCurrentIndex(fi);
    m_updating = false;
}

bank::CsvMapping ImportDialog::mappingFromUi() const
{
    bank::CsvMapping m = m_parse.csvMapping;
    m.date = m_cDate->currentData().toInt();
    m.description = m_cDesc->currentData().toInt();
    m.amount = m_cAmount->currentData().toInt();
    m.debit = m_cDebit->currentData().toInt();
    m.credit = m_cCredit->currentData().toInt();
    m.dateFormat = m_cFormat->currentData().toString();
    return m;
}

void ImportDialog::reparse(bool fromUi)
{
    if (m_data.isEmpty()) return;
    const Account acc = currentAccount();
    bank::CsvMapping mapping;
    if (fromUi) mapping = mappingFromUi();
    else mapping = bank::CsvMapping::fromJson(acc.csvMapping);
    const bool useMapping = mapping.isValid();
    m_parse = bank::parseData(m_data, m_path, useMapping ? &mapping : nullptr);
    if (useMapping && !m_parse.ok() && !fromUi) m_parse = bank::parseData(m_data, m_path, nullptr); // saved mapping doesn't fit this file

    const bool isCsv = m_parse.format == QLatin1String("CSV");
    m_csvBox->setVisible(isCsv);
    if (isCsv && !fromUi) fillMappingCombos();

    if (!m_parse.ok()) {
        m_items.clear();
        rebuildTable();
        m_info->setText(ui::colorSpan(m_parse.error, Theme::c().badText, true));
        return;
    }
    QDate first, last;
    for (const auto &l : m_parse.lines) {
        if (!first.isValid() || l.date < first) first = l.date;
        if (!last.isValid() || l.date > last) last = l.date;
    }
    QString info = QStringLiteral("<b>%1 statement</b> · %2 transactions from %3 to %4")
                       .arg(m_parse.format)
                       .arg(m_parse.lines.size())
                       .arg(util::fmtDate(first), util::fmtDate(last));
    if (!m_parse.accountHint.isEmpty()) info += QStringLiteral(" · account %1").arg(m_parse.accountHint.right(8).toHtmlEscaped());
    if (m_parse.looksLikeCard && !acc.isCard() && !acc.id.isEmpty()) info += QStringLiteral(" · <i>this looks like a credit-card statement</i>");
    if (acc.id.isEmpty()) info += ui::colorSpan(QStringLiteral(" — choose or create the account it belongs to"), Theme::c().badText, true);
    m_info->setText(info);
    prepareItems();
}

void ImportDialog::prepareItems()
{
    Account acc = currentAccount();
    acc.flipSigns = m_flip->isChecked();
    m_items = m_parse.ok() ? bank::prepare(Store::instance(), acc, m_parse.lines) : QVector<bank::Proposed>();
    rebuildTable();
}

void ImportDialog::rebuildTable()
{
    m_updating = true;
    const auto &th = Theme::c();
    m_table->setRowCount(m_items.size());
    QFont boldFont = m_table->font();
    boldFont.setWeight(QFont::DemiBold);
    for (int r = 0; r < m_items.size(); ++r) {
        const auto &p = m_items[r];
        auto *d = new QTableWidgetItem(util::fmtDate(p.tx.date));
        d->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
        d->setCheckState(p.include ? Qt::Checked : Qt::Unchecked);
        m_table->setItem(r, ColDate, d);
        auto *desc = new QTableWidgetItem(p.tx.description);
        desc->setToolTip(p.line.description + (p.line.memo.isEmpty() ? QString() : QStringLiteral("\n") + p.line.memo));
        m_table->setItem(r, ColDesc, desc);
        auto *amt = new QTableWidgetItem((p.tx.type == TxType::Income ? QStringLiteral("+") : QStringLiteral("−")) + util::money(p.tx.amount));
        amt->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        amt->setFont(boldFont);
        amt->setForeground(p.tx.type == TxType::Income ? th.goodText : th.text);
        m_table->setItem(r, ColAmount, amt);
        auto *combo = new QComboBox;
        fillCategoryCombo(combo, p.tx.categoryId, true, p.tx.type);
        connect(combo, &QComboBox::currentIndexChanged, this, [this, r, combo]() {
            if (r < m_items.size()) m_items[r].tx.categoryId = combo->currentData().toString();
        });
        m_table->setCellWidget(r, ColCategory, combo);
        auto *note = new QTableWidgetItem(p.note);
        note->setToolTip(p.note);
        const bool flagged = p.status != bank::Proposed::Status::New;
        note->setForeground(p.status == bank::Proposed::Status::PossibleDuplicate ? th.badText : flagged ? th.muted : th.text2);
        m_table->setItem(r, ColNote, note);
        if (p.status == bank::Proposed::Status::Duplicate || p.status == bank::Proposed::Status::Ignored) {
            desc->setForeground(th.muted);
            d->setForeground(th.muted);
        }
    }
    m_updating = false;
    updateSummary();
}

void ImportDialog::updateSummary()
{
    int include = 0, dup = 0, maybe = 0, ignored = 0;
    for (const auto &p : m_items) {
        if (p.include) ++include;
        if (p.status == bank::Proposed::Status::Duplicate) ++dup;
        if (p.status == bank::Proposed::Status::PossibleDuplicate) ++maybe;
        if (p.status == bank::Proposed::Status::Ignored) ++ignored;
    }
    QStringList parts;
    parts << QStringLiteral("<b>%1 to import</b>").arg(include);
    if (dup) parts << QStringLiteral("%1 already imported").arg(dup);
    if (maybe) parts << ui::colorSpan(QStringLiteral("%1 possible duplicate%2 (unticked — tick to import anyway)").arg(maybe).arg(maybe == 1 ? QString() : QStringLiteral("s")), Theme::c().badText);
    if (ignored) parts << QStringLiteral("%1 ignored by rules").arg(ignored);
    m_summary->setText(m_items.isEmpty() ? QString() : parts.join(QStringLiteral(" · ")));
    m_importBtn->setText(include ? QStringLiteral("Import %1 transaction%2").arg(include).arg(include == 1 ? QString() : QStringLiteral("s")) : QStringLiteral("Import"));
    m_importBtn->setEnabled(include > 0 && !currentAccount().id.isEmpty());
    m_ruleBtn->setEnabled(!m_items.isEmpty());
}

void ImportDialog::makeRuleFromSelection()
{
    const int r = m_table->currentRow();
    if (r < 0 || r >= m_items.size()) {
        QMessageBox::information(this, windowTitle(), QStringLiteral("Select a line first."));
        return;
    }
    const QString key = bank::payeeKey(m_items[r].line.description);
    RuleDialog dlg(this);
    dlg.setSuggestion(key.isEmpty() ? m_items[r].tx.description : key, m_items[r].tx.categoryId);
    if (dlg.exec() == QDialog::Accepted) prepareItems(); // apply the new rule to the list
}

void ImportDialog::doImport()
{
    Account acc = currentAccount();
    if (acc.id.isEmpty()) {
        QMessageBox::information(this, windowTitle(), QStringLiteral("Choose the account or card this statement belongs to."));
        return;
    }
    // Remember the CSV columns and sign convention for next time
    bool changed = acc.flipSigns != m_flip->isChecked();
    acc.flipSigns = m_flip->isChecked();
    if (m_parse.format == QLatin1String("CSV") && m_parse.csvMapping.isValid()) {
        const QString mj = (m_csvBox->isVisible() ? mappingFromUi() : m_parse.csvMapping).toJson();
        if (mj != acc.csvMapping) {
            acc.csvMapping = mj;
            changed = true;
        }
    }
    if (changed) Store::instance().saveAccount(acc);
    QString err;
    m_imported = bank::commit(Store::instance(), m_items, &err);
    if (m_imported < 0) {
        QMessageBox::warning(this, windowTitle(), QStringLiteral("Import failed: %1").arg(err));
        return;
    }
    accept();
}
