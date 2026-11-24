#pragma once

#include <QDialog>
#include <QNetworkProxy>
#include <QString>

#include <functional>

#include "core/UpdateChecker.h"
#include "platform/Autostart.h"

class QCheckBox;
class QComboBox;
class QKeySequenceEdit;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;

class SettingsDialog : public QDialog
{
    Q_OBJECT

public:
    // 返回 false 时通过 error 给出原因，对话框据此回滚编辑内容。
    using HotkeyApplier = std::function<bool(const QString &sequence, QString *error)>;

    explicit SettingsDialog(QWidget *parent = nullptr, HotkeyApplier hotkeyApplier = {});

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
    bool applyHotkeySequence(const QString &sequence);
    void restoreHotkeyEdit();
    void setBusy(bool busy);
    void setStatus(const QString &text);

    UpdateChecker *m_checker = nullptr;
    QLabel *m_statusLabel = nullptr;
    QPushButton *m_checkButton = nullptr;
    QPushButton *m_closeButton = nullptr;
    QCheckBox *m_autostartCheck = nullptr;
    QSpinBox *m_retentionSpin = nullptr;
    QKeySequenceEdit *m_hotkeyEdit = nullptr;
    QComboBox *m_proxyMode = nullptr;
    QComboBox *m_proxyType = nullptr;
    QLineEdit *m_proxyHost = nullptr;
    QSpinBox *m_proxyPort = nullptr;
    Autostart m_autostart;
    ReleaseInfo m_pending;
    QString m_downloadPath;
    QString m_appliedSequence;
    HotkeyApplier m_hotkeyApplier;
    bool m_busy = false;
};
