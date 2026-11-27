#pragma once

#include <QPoint>
#include <QString>
#include <QWidget>

class QEvent;
class QHideEvent;
class QKeyEvent;
class QLabel;
class QLineEdit;
class QPushButton;
class QShowEvent;

// 贴在触发位置下方的小型输入面板，替代系统对话框。
class FolderEditPopup : public QWidget
{
    Q_OBJECT

public:
    explicit FolderEditPopup(QWidget *parent = nullptr);
    ~FolderEditPopup() override;

    static QString getText(QWidget *parent, const QString &title, const QString &initial,
                           const QPoint &anchor, bool *ok);

signals:
    void finished();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    void finish(int result);
    void positionBelow(const QPoint &anchor);

    QLabel *m_titleLabel = nullptr;
    QLineEdit *m_edit = nullptr;
    QPushButton *m_okButton = nullptr;
    int m_result = 0;
    bool m_done = false;
};
