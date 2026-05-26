#pragma once

#include <QDialog>

QT_BEGIN_NAMESPACE
namespace Ui { class SettingsDialog; }
QT_END_NAMESPACE

class OAuthManager;

class SettingsDialog : public QDialog {
    Q_OBJECT

public:
    explicit SettingsDialog(OAuthManager* oauth, QWidget* parent = nullptr);
    ~SettingsDialog() override;

protected:
    void changeEvent(QEvent* e) override;

private slots:
    void onAddAccount();
    void onRemoveAccount();
    void onSetDefaultAccount();
    void onAccepted();

private:
    void reloadAccounts();
    void loadSettings();
    void saveSettings();

    QString currentAccountEmail() const;

    Ui::SettingsDialog* ui;
    OAuthManager*       m_oauth;
};
