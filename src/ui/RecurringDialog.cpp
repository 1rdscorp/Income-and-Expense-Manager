#include "RecurringDialog.h"

#include "Theme.h"
#include "Widgets.h"
#include "core/Defaults.h"
#include "core/Recurrence.h"
#include "core/Store.h"
#include "core/Util.h"

#include <QComboBox>
#include <QCompleter>
#include <QDateEdit>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace {
QLabel *fieldLabel(const QString &text)
{
    auto *l = new QLabel(text);
    l->setStyleSheet(QStringLiteral("font-weight:600;"));
    return l;
}
} // namespace

RecurringDialog::RecurringDialog(QWidget *parent)
    : QDialog(parent)
{
    setMinimumWidth(620);
    auto *root = new QVBoxLayout(this);
    root->setSpacing(10);
    m_type = new SegmentedControl({{QStringLiteral("expense"), QStringLiteral("   Expense   ")}, {QStringLiteral("income"), QStringLiteral("   Income   ")}});
    root->addWidget(m_type, 0, Qt::AlignLeft);

    auto *g = new QGridLayout;
    g->setHorizontalSpacing(14);
    g->setVerticalSpacing(4);
    int row = 0;
    auto gap = [&]() { g->addItem(new QSpacerItem(1, 8), row++, 0); };

    m_amount = new QLineEdit;
    m_amount->setObjectName(QStringLiteral("amountEdit"));
    m_amount->setPlaceholderText(QStringLiteral("0.00"));
    m_category = new QComboBox;
    m_category->setObjectName(QStringLiteral("categoryCombo"));
    m_category->setMaxVisibleItems(16);
    g->addWidget(fieldLabel(QStringLiteral("Amount each time (%1)").arg(util::currencyCode())), row, 0);
    g->addWidget(fieldLabel(QStringLiteral("Category")), row++, 1);
    g->addWidget(m_amount, row, 0);
    g->addWidget(m_category, row++, 1);
    gap();

    m_desc = new QLineEdit;
    m_desc->setObjectName(QStringLiteral("descEdit"));
    m_desc->setMaxLength(200);
    m_desc->setPlaceholderText(QStringLiteral("e.g. Shop rent, Business internet"));
    g->addWidget(fieldLabel(QStringLiteral("Description")), row++, 0, 1, 2);
    g->addWidget(m_desc, row++, 0, 1, 2);
    gap();

    m_vendor = new QLineEdit;
    m_vendor->setMaxLength(120);
    auto *comp = new QCompleter(Store::instance().knownVendors(), m_vendor);
    comp->setCaseSensitivity(Qt::CaseInsensitive);
    comp->setFilterMode(Qt::MatchContains);
    m_vendor->setCompleter(comp);
    m_payment = new QComboBox;
    m_payment->addItem(QStringLiteral("—"), QString());
    for (const auto &p : defaults::paymentMethods()) m_payment->addItem(p, p);
    g->addWidget(fieldLabel(QStringLiteral("Vendor / payee")), row, 0);
    g->addWidget(fieldLabel(QStringLiteral("Payment method")), row++, 1);
    g->addWidget(m_vendor, row, 0);
    g->addWidget(m_payment, row++, 1);
    gap();

    // Schedule
    m_frequency = new QComboBox;
    m_frequency->setObjectName(QStringLiteral("freqCombo"));
    for (Frequency f : {Frequency::Monthly, Frequency::Weekly, Frequency::Quarterly, Frequency::Yearly})
        m_frequency->addItem(frequencyLabel(f), frequencyToString(f));
    m_start = new QDateEdit(QDate::currentDate());
    m_start->setObjectName(QStringLiteral("startDate"));
    m_start->setCalendarPopup(true);
    m_start->setDisplayFormat(QStringLiteral("ddd, d MMM yyyy"));
    g->addWidget(fieldLabel(QStringLiteral("Repeats")), row, 0);
    g->addWidget(fieldLabel(QStringLiteral("First payment date")), row++, 1);
    g->addWidget(m_frequency, row, 0);
    g->addWidget(m_start, row++, 1);
    gap();

    m_ends = new QComboBox;
    m_ends->setObjectName(QStringLiteral("endsCombo"));
    m_ends->addItem(QStringLiteral("Never"), false);
    m_ends->addItem(QStringLiteral("On a date"), true);
    m_end = new QDateEdit(QDate::currentDate().addYears(1));
    m_end->setObjectName(QStringLiteral("endDate"));
    m_end->setCalendarPopup(true);
    m_end->setDisplayFormat(QStringLiteral("ddd, d MMM yyyy"));
    m_end->setEnabled(false);
    g->addWidget(fieldLabel(QStringLiteral("Ends")), row, 0);
    g->addWidget(fieldLabel(QStringLiteral("Last payment on or before")), row++, 1);
    g->addWidget(m_ends, row, 0);
    g->addWidget(m_end, row++, 1);
    gap();

    m_notes = new QPlainTextEdit;
    m_notes->setFixedHeight(54);
    m_notes->setTabChangesFocus(true);
    g->addWidget(fieldLabel(QStringLiteral("Notes")), row++, 0, 1, 2);
    g->addWidget(m_notes, row++, 0, 1, 2);
    g->setColumnStretch(0, 1);
    g->setColumnStretch(1, 1);
    root->addLayout(g);

    auto *previewCard = new QFrame;
    previewCard->setObjectName(QStringLiteral("card"));
    auto *pl = new QHBoxLayout(previewCard);
    pl->setContentsMargins(12, 10, 12, 10);
    auto *icon = new QLabel;
    icon->setPixmap(Theme::iconPixmap(QStringLiteral("repeat"), Theme::c().accent, 20));
    m_preview = new QLabel;
    m_preview->setWordWrap(true);
    m_preview->setTextFormat(Qt::RichText);
    pl->addWidget(icon, 0, Qt::AlignTop);
    pl->addWidget(m_preview, 1);
    root->addWidget(previewCard);

    auto *bar = new QHBoxLayout;
    m_deleteBtn = ui::button(QStringLiteral("Delete"), QStringLiteral("trash"), QStringLiteral("dangerText"));
    auto *cancel = ui::button(QStringLiteral("Cancel"));
    auto *saveBtn = ui::button(QStringLiteral("Save"), QString(), QStringLiteral("primary"));
    saveBtn->setAccessibleName(QStringLiteral("save"));
    saveBtn->setDefault(true);
    bar->addWidget(m_deleteBtn);
    bar->addStretch(1);
    bar->addWidget(cancel);
    bar->addWidget(saveBtn);
    root->addLayout(bar);

    connect(m_type, &SegmentedControl::changed, this, [this]() { fillCategories(); });
    connect(m_frequency, &QComboBox::currentIndexChanged, this, &RecurringDialog::updatePreview);
    connect(m_start, &QDateEdit::dateChanged, this, &RecurringDialog::updatePreview);
    connect(m_end, &QDateEdit::dateChanged, this, &RecurringDialog::updatePreview);
    connect(m_amount, &QLineEdit::textChanged, this, &RecurringDialog::updatePreview);
    connect(m_ends, &QComboBox::currentIndexChanged, this, [this]() {
        m_end->setEnabled(m_ends->currentData().toBool());
        updatePreview();
    });
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(saveBtn, &QPushButton::clicked, this, &RecurringDialog::save);
    connect(m_deleteBtn, &QPushButton::clicked, this, &RecurringDialog::remove);

    setNew(TxType::Expense);
}

void RecurringDialog::setNew(TxType type)
{
    setWindowTitle(QStringLiteral("New recurring transaction"));
    m_editing = false;
    m_rule = Recurring();
    m_rule.type = type;
    m_type->setValue(typeToString(type));
    m_deleteBtn->hide();
    fillCategories();
    updatePreview();
    m_amount->setFocus();
}

void RecurringDialog::setRule(const Recurring &r)
{
    setWindowTitle(QStringLiteral("Edit recurring transaction"));
    m_editing = true;
    m_rule = r;
    m_type->setValue(typeToString(r.type));
    m_amount->setText(util::amountPlain(r.amount));
    m_desc->setText(r.description);
    m_vendor->setText(r.vendor);
    const int pi = m_payment->findData(r.paymentMethod);
    m_payment->setCurrentIndex(pi >= 0 ? pi : 0);
    m_frequency->setCurrentIndex(std::max(0, m_frequency->findData(frequencyToString(r.frequency))));
    m_start->setDate(r.startDate);
    m_ends->setCurrentIndex(r.endDate.isValid() ? 1 : 0);
    if (r.endDate.isValid()) m_end->setDate(r.endDate);
    m_notes->setPlainText(r.notes);
    m_deleteBtn->show();
    fillCategories();
    updatePreview();
}

void RecurringDialog::fillCategories()
{
    const TxType type = typeFromString(m_type->value());
    const QString keep = m_category->currentData().toString();
    m_category->clear();
    m_category->addItem(QStringLiteral("Select a category…"), QString());
    for (const auto &c : Store::instance().categoriesOfType(type)) m_category->addItem(c.icon + QStringLiteral("   ") + c.name, c.id);
    int idx = m_category->findData(m_rule.categoryId);
    if (idx <= 0) idx = m_category->findData(keep);
    m_category->setCurrentIndex(idx > 0 ? idx : 0);
}

void RecurringDialog::updatePreview()
{
    Recurring r = m_rule;
    r.frequency = frequencyFromString(m_frequency->currentData().toString());
    r.startDate = m_start->date();
    r.day = r.startDate.day();
    r.endDate = m_ends->currentData().toBool() ? m_end->date() : QDate();
    const QDate today = QDate::currentDate();
    const qint64 amount = util::parseAmount(m_amount->text());
    const QString amt = amount > 0 ? util::money(amount) + QStringLiteral(" ") : QString();

    QString html = QStringLiteral("<b>%1%2</b>").arg(amt.toHtmlEscaped(), recurrence::describe(r).toLower().toHtmlEscaped());
    if (amt.isEmpty()) html = QStringLiteral("<b>%1</b>").arg(recurrence::describe(r).toHtmlEscaped());

    const bool scheduleChanged = !m_editing || r.frequency != m_rule.frequency || r.startDate != m_rule.startDate;
    if (m_editing && !scheduleChanged) {
        r.nextDate = m_rule.nextDate;
    } else if (m_editing && Store::instance().recurringPostedCount(m_rule.id) > 0) {
        r.nextDate = recurrence::firstOnOrAfter(r, std::max(today, r.startDate));
    } else {
        r.nextDate = r.startDate;
        // Payments already due will be recorded as soon as the rule is saved.
        const auto past = recurrence::occurrences(r, r.startDate, today, 1000);
        if (!past.isEmpty()) {
            html += QStringLiteral("<br><span style='color:%1'>%2 past payment%3 (%4 – %5) will be recorded right away.</span>")
                        .arg(Theme::c().text2.name())
                        .arg(past.size())
                        .arg(past.size() == 1 ? QString() : QStringLiteral("s"), util::fmtDate(past.first()), util::fmtDate(past.last()));
            r.nextDate = recurrence::advance(past.last(), r.frequency, r.day);
        }
    }
    QStringList next;
    for (const QDate &d : recurrence::occurrences(r, r.nextDate, today.addYears(5), 3)) next << util::fmtDate(d);
    if (next.isEmpty()) html += QStringLiteral("<br>No more payments — the end date has passed.");
    else html += QStringLiteral("<br>Next: %1").arg(next.join(QStringLiteral(", ")).toHtmlEscaped());
    if (m_editing) {
        const int n = Store::instance().recurringPostedCount(m_rule.id);
        html += QStringLiteral("<br><span style='color:%1'>%2 payment%3 recorded so far. Changes apply to future payments only.</span>")
                    .arg(Theme::c().muted.name())
                    .arg(n)
                    .arg(n == 1 ? QString() : QStringLiteral("s"));
    }
    m_preview->setText(html);
}

void RecurringDialog::save()
{
    Recurring r = m_rule;
    r.amount = util::parseAmount(m_amount->text());
    if (r.amount <= 0) {
        QMessageBox::warning(this, windowTitle(), QStringLiteral("Enter an amount greater than zero."));
        m_amount->setFocus();
        return;
    }
    r.categoryId = m_category->currentData().toString();
    if (r.categoryId.isEmpty()) {
        QMessageBox::warning(this, windowTitle(), QStringLiteral("Choose a category."));
        m_category->setFocus();
        return;
    }
    r.type = typeFromString(m_type->value());
    r.description = m_desc->text().trimmed();
    r.vendor = m_vendor->text().trimmed();
    r.paymentMethod = m_payment->currentData().toString();
    r.notes = m_notes->toPlainText().trimmed();
    r.frequency = frequencyFromString(m_frequency->currentData().toString());
    r.startDate = m_start->date();
    r.day = r.startDate.day();
    r.endDate = m_ends->currentData().toBool() ? m_end->date() : QDate();
    if (r.description.isEmpty() && r.vendor.isEmpty()) {
        if (const Category *c = Store::instance().category(r.categoryId)) r.description = c->name;
    }
    QString err;
    if (!Store::instance().saveRecurring(r, &err)) {
        QMessageBox::warning(this, windowTitle(), err);
        return;
    }
    accept();
}

void RecurringDialog::remove()
{
    if (!m_editing) return;
    const int n = Store::instance().recurringPostedCount(m_rule.id);
    if (QMessageBox::question(this, QStringLiteral("Delete recurring transaction"),
                              QStringLiteral("Stop “%1” and delete this recurring rule?\n\nThe %2 payment(s) already recorded will be kept.").arg(m_rule.title()).arg(n))
        != QMessageBox::Yes)
        return;
    QString err;
    if (!Store::instance().deleteRecurring(m_rule.id, &err)) {
        QMessageBox::warning(this, windowTitle(), err);
        return;
    }
    m_deleted = true;
    accept();
}
