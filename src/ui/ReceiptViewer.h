#pragma once

#include "core/Models.h"
#include <QDialog>

class QLabel;
class QScrollArea;

/* Shows image receipts in-app; other files (PDF etc.) open in the default app. */
class ReceiptViewer : public QDialog
{
    Q_OBJECT
public:
    static void display(QWidget *parent, const Receipt &r);
    static bool openExternally(QWidget *parent, const Receipt &r);
    static void saveAs(QWidget *parent, const Receipt &r);

protected:
    void resizeEvent(QResizeEvent *e) override;

private:
    ReceiptViewer(QWidget *parent, const Receipt &r);
    void rescale();

    Receipt m_receipt;
    QPixmap m_pixmap;
    QLabel *m_image;
    QScrollArea *m_scroll;
    bool m_fit = true;
};
