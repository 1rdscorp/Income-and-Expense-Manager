#pragma once

#include <QFrame>
#include <QLabel>
#include <QPushButton>
#include <QVector>
#include <QWidget>

class QBoxLayout;
class QHBoxLayout;
class QVBoxLayout;
class QTableWidget;
class QButtonGroup;

/* A rounded "card" panel with an optional header row. */
class Card : public QFrame
{
    Q_OBJECT
public:
    explicit Card(const QString &title = QString(), const QString &subtitle = QString(), QWidget *parent = nullptr);
    QVBoxLayout *body() const { return m_body; }
    QHBoxLayout *header() const { return m_header; }
    void setTitle(const QString &title);
    void setSubtitle(const QString &sub);

private:
    QVBoxLayout *m_body;
    QHBoxLayout *m_header;
    QLabel *m_title;
    QLabel *m_sub;
};

class KpiCard : public QFrame
{
    Q_OBJECT
public:
    explicit KpiCard(QWidget *parent = nullptr);
    void set(const QString &label, const QString &value, const QString &metaHtml = QString(),
             const QColor &swatch = QColor(), bool negative = false);

private:
    QLabel *m_label;
    QLabel *m_value;
    QLabel *m_meta;
};

class SegmentedControl : public QFrame
{
    Q_OBJECT
public:
    explicit SegmentedControl(const QVector<QPair<QString, QString>> &options, QWidget *parent = nullptr);
    QString value() const { return m_value; }
    void setValue(const QString &v);
    void setLabel(const QString &value, const QString &label);

signals:
    void changed(const QString &value);

private:
    QString m_value;
    QVector<QPushButton *> m_buttons;
    QButtonGroup *m_group;
};

class PeriodNav : public QWidget
{
    Q_OBJECT
public:
    explicit PeriodNav(QWidget *parent = nullptr);
    void setLabel(const QString &text);

signals:
    void stepped(int delta);

private:
    QLabel *m_label;
    QPushButton *m_prev;
    QPushButton *m_next;
};

/* Ranked horizontal bars (top spending, last 7 days). */
class HBarList : public QWidget
{
    Q_OBJECT
public:
    struct Row {
        QString icon;
        QString name;
        QString amount;
        QString extra;     // small text before amount (e.g. "+$120 income")
        QString sub;       // muted line under the bar
        QString warn;      // red warning text appended to sub (e.g. over budget)
        double fraction = 0.0;
        bool over = false;
    };
    explicit HBarList(QWidget *parent = nullptr);
    void setRows(const QVector<Row> &rows, const QString &emptyText = QString());
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent *) override;

private:
    int rowHeight(const Row &r) const;
    QVector<Row> m_rows;
    QString m_empty;
};

class Banner : public QFrame
{
    Q_OBJECT
public:
    explicit Banner(QWidget *parent = nullptr);
    void setText(const QString &html);
    QPushButton *button() const { return m_button; }

private:
    QLabel *m_icon;
    QLabel *m_text;
    QPushButton *m_button;
};

namespace ui {
QPushButton *button(const QString &text, const QString &icon = QString(), const QString &objectName = QString(), QWidget *parent = nullptr);
QLabel *muted(const QString &text, QWidget *parent = nullptr);
void clearLayout(QLayout *layout);
void fitTableHeight(QTableWidget *table);
QString colorSpan(const QString &text, const QColor &color, bool bold = false);
} // namespace ui
