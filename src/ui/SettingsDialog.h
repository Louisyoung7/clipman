#pragma once

#include <QDialog>
#include <QNetworkProxy>
#include <QString>

#include <functional>

#include "core/UpdateChecker.h"
#include "platform/Autostart.h"

class QCheckBox;
class QComboBox;
class QEvent;
class QKeySequenceEdit;
class QLabel;
class QLineEdit;
class QPushButton;
class QShowEvent;
class QSpinBox;

class SettingsDialog : public QDialog
{
    Q_OBJECT

public:
    // 返回 false 时通过 error 给出原因，对话框据此回滚编辑内容。
    using HotkeyApplier = std::function<bool(const QString &sequence, QString *error)>;

    explicit SettingsDialog(QWidget *parent = nullptr, HotkeyApplier hotkeyApplier = {},
                            HotkeyApplier settingsHotkeyApplier = {});
    ~SettingsDialog() override;

protected:
    void done(int result) override;
    void changeEvent(QEvent *event) override;
    void showEvent(QShowEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void checkForUpdates();
    void onUpdateAvailable(const ReleaseInfo &info);
    void startDownload();
    void startInstall();
    void loadSettings();
    void saveSettings();
    void updateProxyControls();
    QNetworkProxy resolvedProxy() const;
    bool applySequence(QKeySequenceEdit *edit, const HotkeyApplier &applier, QString *applied,
                       const QString &otherApplied, const QString &otherName);
    static void restoreSequence(QKeySequenceEdit *edit, const QString &sequence);
    void syncPendingSequences();
    void positionNearCursor();
    void setBusy(bool busy);
    void setStatus(const QString &text);

    UpdateChecker *m_checker = nullptr;
    QLabel *m_statusLabel = nullptr;
    QPushButton *m_checkButton = nullptr;
    QPushButton *m_closeButton = nullptr;
    QCheckBox *m_autostartCheck = nullptr;
    QSpinBox *m_retentionSpin = nullptr;
    QKeySequenceEdit *m_hotkeyEdit = nullptr;
    QKeySequenceEdit *m_settingsHotkeyEdit = nullptr;
    QComboBox *m_proxyMode = nullptr;
    QComboBox *m_proxyType = nullptr;
    QLineEdit *m_proxyHost = nullptr;
    QSpinBox *m_proxyPort = nullptr;
    Autostart m_autostart;
    ReleaseInfo m_pending;
    QString m_downloadPath;
    QString m_appliedSequence;
    QString m_appliedSettingsSequence;
    HotkeyApplier m_hotkeyApplier;
    HotkeyApplier m_settingsHotkeyApplier;
    bool m_busy = false;
    bool m_repositionedAfterShow = false;
};
