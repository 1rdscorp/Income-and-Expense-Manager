#pragma once

#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class SegmentedControl;

class SettingsPage : public QWidget
{
    Q_OBJECT
public:
    explicit SettingsPage(QWidget *parent = nullptr);

public slots:
    void refresh();

private:
    QLineEdit *m_name;
    QComboBox *m_currency;
    SegmentedControl *m_theme;
    QCheckBox *m_compress;
    QComboBox *m_reminder;
    QLabel *m_dataDir;
    QLabel *m_usage;
    QPushButton *m_sample;
};
