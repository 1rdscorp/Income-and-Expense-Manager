#pragma once

#include <QStringList>
#include <QVector>
#include <QWidget>

/* Grouped vertical bar chart (income vs expenses) with hover tooltip and click. */
class BarChart : public QWidget
{
    Q_OBJECT
public:
    explicit BarChart(QWidget *parent = nullptr);
    void setData(const QStringList &labels, const QStringList &fullLabels, const QVector<qint64> &income, const QVector<qint64> &expense);
    QSize sizeHint() const override { return QSize(600, 260); }
    QSize minimumSizeHint() const override { return QSize(280, 220); }
    void setClickable(bool c) { m_clickable = c; }

signals:
    void columnClicked(int index);

protected:
    void paintEvent(QPaintEvent *) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void leaveEvent(QEvent *) override;
    void mousePressEvent(QMouseEvent *e) override;

private:
    QRectF plotRect() const;
    int columnAt(const QPointF &pos) const;

    QStringList m_labels, m_full;
    QVector<qint64> m_income, m_expense;
    int m_hover = -1;
    bool m_clickable = true;
};
