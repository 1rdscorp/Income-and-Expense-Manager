#pragma once

#include "core/Models.h"
#include <QWidget>

class Banner;
class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QRadioButton;
class QSpinBox;
class QWidget;

class BackupPage : public QWidget
{
    Q_OBJECT
public:
    explicit BackupPage(QWidget *parent = nullptr);

public slots:
    void refresh();
    void backupNow(bool includeReceipts);
    void chooseRestoreFile();
    void exportCsv();
    bool loadRestoreFile(const QString &path);

private:
    void restore();
    void saveAutoSettings();

    Banner *m_banner;
    QLabel *m_stats;
    QLabel *m_last;
    QCheckBox *m_autoOn;
    QLineEdit *m_autoDir;
    QSpinBox *m_autoKeep;
    QPushButton *m_autoBrowse;
    QPushButton *m_autoOpen;
    QLabel *m_restoreInfo;
    QWidget *m_restoreOptions;
    QRadioButton *m_replace;
    QRadioButton *m_merge;
    BackupData m_pending;
    QString m_pendingName;
    bool m_hasPending = false;
};
