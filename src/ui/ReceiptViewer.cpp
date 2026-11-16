#include "ReceiptViewer.h"

#include "Widgets.h"
#include "core/Util.h"

#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QStandardPaths>
#include <QUrl>
#include <QVBoxLayout>

namespace {
QString safeName(const QString &name)
{
    QString n = name;
    for (QChar &c : n)
        if (QStringLiteral("\\/:*?\"<>|").contains(c) || c.unicode() < 32) c = QLatin1Char('_');
    return n.isEmpty() ? QStringLiteral("receipt") : n;
}
} // namespace

ReceiptViewer::ReceiptViewer(QWidget *parent, const Receipt &r)
    : QDialog(parent)
    , m_receipt(r)
{
    setWindowTitle(r.name);
    resize(820, 680);
    auto *l = new QVBoxLayout(this);
    m_scroll = new QScrollArea;
    m_scroll->setAlignment(Qt::AlignCenter);
    m_scroll->setWidgetResizable(false);
    m_image = new QLabel;
    m_image->setAlignment(Qt::AlignCenter);
    m_scroll->setWidget(m_image);
    l->addWidget(m_scroll, 1);
    m_pixmap.loadFromData(r.data);

    auto *bar = new QHBoxLayout;
    bar->addWidget(ui::muted(QStringLiteral("%1 · %2 · added %3").arg(r.mime, util::fmtBytes(r.data.size()), util::fmtDateTime(r.createdAt))), 1);
    auto *fit = ui::button(QStringLiteral("Actual size"));
    auto *open = ui::button(QStringLiteral("Open in default app"), QStringLiteral("external"));
    auto *save = ui::button(QStringLiteral("Save as…"), QStringLiteral("download"), QStringLiteral("primary"));
    auto *close = ui::button(QStringLiteral("Close"));
    bar->addWidget(fit);
    bar->addWidget(open);
    bar->addWidget(save);
    bar->addWidget(close);
    l->addLayout(bar);
    connect(fit, &QPushButton::clicked, this, [this, fit]() {
        m_fit = !m_fit;
        fit->setText(m_fit ? QStringLiteral("Actual size") : QStringLiteral("Fit to window"));
        rescale();
    });
    connect(open, &QPushButton::clicked, this, [this]() { openExternally(this, m_receipt); });
    connect(save, &QPushButton::clicked, this, [this]() { saveAs(this, m_receipt); });
    connect(close, &QPushButton::clicked, this, &QDialog::accept);
    rescale();
}

void ReceiptViewer::rescale()
{
    if (m_pixmap.isNull()) {
        m_image->setText(QStringLiteral("This image could not be displayed."));
        m_image->adjustSize();
        return;
    }
    const QSize avail = m_scroll->viewport()->size() - QSize(4, 4);
    QPixmap pm = m_pixmap;
    if (m_fit && (pm.width() > avail.width() || pm.height() > avail.height()))
        pm = m_pixmap.scaled(avail, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    m_image->setPixmap(pm);
    m_image->resize(pm.size());
}

void ReceiptViewer::resizeEvent(QResizeEvent *e)
{
    QDialog::resizeEvent(e);
    rescale();
}

void ReceiptViewer::display(QWidget *parent, const Receipt &r)
{
    if (!r.isImage() || QPixmap().loadFromData(r.data) == false) {
        openExternally(parent, r);
        return;
    }
    ReceiptViewer v(parent, r);
    v.exec();
}

bool ReceiptViewer::openExternally(QWidget *parent, const Receipt &r)
{
    const QString dir = QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation)).filePath(QStringLiteral("ledgerly-receipts"));
    QDir().mkpath(dir);
    const QString path = QDir(dir).filePath(r.id.left(8) + QLatin1Char('-') + safeName(r.name));
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(r.data) != r.data.size()) {
        QMessageBox::warning(parent, QStringLiteral("Open receipt"), QStringLiteral("Could not write a temporary copy: %1").arg(f.errorString()));
        return false;
    }
    f.close();
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(path))) {
        QMessageBox::warning(parent, QStringLiteral("Open receipt"), QStringLiteral("No application is available to open %1.").arg(r.name));
        return false;
    }
    return true;
}

void ReceiptViewer::saveAs(QWidget *parent, const Receipt &r)
{
    const QString def = QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)).filePath(safeName(r.name));
    const QString path = QFileDialog::getSaveFileName(parent, QStringLiteral("Save receipt"), def);
    if (path.isEmpty()) return;
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(r.data) != r.data.size())
        QMessageBox::warning(parent, QStringLiteral("Save receipt"), f.errorString());
}
