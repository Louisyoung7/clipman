#pragma once

#include <QDialog>
#include <QNetworkProxy>

#include "core/UpdateChecker.h"
#include "platform/Autostart.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;

class SettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SettingsDialog(QWidget *parent = nullptr);

protected:
    void done(int result) override;

private:
    void checkForUpdates();
    void onUpdateAvailable(const ReleaseInfo &info);
    void startDownload();
    void startInstall();
    void loadSettings();
    void saveSettings();
    void updateProxyControls();
    QNetworkProxy resolvedProxy() const;
    void setBusy(bool busy);
    void setStatus(const QString &text);

    UpdateChecker *m_checker = nullptr;
    QLabel *m_statusLabel = nullptr;
    QPushButton *m_checkButton = nullptr;
    QPushButton *m_closeButton = nullptr;
    QCheckBox *m_autostartCheck = nullptr;
    QComboBox *m_proxyMode = nullptr;
    QComboBox *m_proxyType = nullptr;
    QLineEdit *m_proxyHost = nullptr;
    QSpinBox *m_proxyPort = nullptr;
    Autostart m_autostart;
    ReleaseInfo m_pending;
    QString m_downloadPath;
    bool m_busy = false;
};
