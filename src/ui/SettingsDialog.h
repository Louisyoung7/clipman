#pragma once

#include <QDialog>

#include "core/UpdateChecker.h"

class QLabel;
class QPushButton;

class SettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SettingsDialog(QWidget *parent = nullptr);

private:
    void checkForUpdates();
    void onUpdateAvailable(const ReleaseInfo &info);
    void startDownload();
    void startInstall();
    void setBusy(bool busy);
    void setStatus(const QString &text);

    UpdateChecker *m_checker = nullptr;
    QLabel *m_statusLabel = nullptr;
    QPushButton *m_checkButton = nullptr;
    QPushButton *m_closeButton = nullptr;
    ReleaseInfo m_pending;
    QString m_downloadPath;
    bool m_busy = false;
};
