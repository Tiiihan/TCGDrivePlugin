#include "settings_dialog.h"
#include "ui_settings_dialog.h"

#include "auth_dialog.h"
#include "gui_manager.h"
#include "../auth/oauth_manager.h"
#include "../core/filesystem_bridge.h"
#include "../storage/config_manager.h"
#include "../utils/logger.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QEvent>
#include <QFont>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPushButton>
#include <QSpinBox>

namespace {

constexpr int kIdxLangUk = 0;
constexpr int kIdxLangEn = 1;

} 

SettingsDialog::SettingsDialog(OAuthManager* oauth, QWidget* parent)
    : QDialog(parent),
      ui(new Ui::SettingsDialog),
      m_oauth(oauth) {
    ui->setupUi(this);

    connect(ui->addAccountButton,    &QPushButton::clicked, this, &SettingsDialog::onAddAccount);
    connect(ui->removeAccountButton, &QPushButton::clicked, this, &SettingsDialog::onRemoveAccount);
    connect(ui->setDefaultButton,    &QPushButton::clicked, this, &SettingsDialog::onSetDefaultAccount);
    connect(ui->buttonBox, &QDialogButtonBox::accepted, this, &SettingsDialog::onAccepted);
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    reloadAccounts();
    loadSettings();
}

SettingsDialog::~SettingsDialog() {
    delete ui;
}

QString SettingsDialog::currentAccountEmail() const {
    auto* item = ui->accountsList->currentItem();
    if (!item) return {};
    const QString stored = item->data(Qt::UserRole).toString();
    return stored.isEmpty() ? item->text() : stored;
}

void SettingsDialog::reloadAccounts() {
    ui->accountsList->clear();
    if (!m_oauth) return;

    const QString def = QString::fromStdWString(
        ConfigManager::instance().defaultAccount());

    for (const std::string& acc : m_oauth->getAccountList()) {
        const QString email = QString::fromStdString(acc);
        QString       label = email;
        if (!def.isEmpty() && email == def) {
            label += tr("  (default)");
        }
        auto* item = new QListWidgetItem(label, ui->accountsList);
        item->setData(Qt::UserRole, email);
        if (!def.isEmpty() && email == def) {
            QFont f = item->font();
            f.setBold(true);
            item->setFont(f);
        }
    }
}

void SettingsDialog::loadSettings() {
    auto& cfg = ConfigManager::instance();
    ui->cacheTtlSpin->setValue(cfg.cacheTtlSeconds());
    ui->languageCombo->setCurrentIndex(cfg.language() == L"en" ? kIdxLangEn : kIdxLangUk);
    ui->logLevelCombo->setCurrentIndex(cfg.logLevel());
    ui->exportDocsCheck->setChecked(cfg.exportDocsAsDocx());
    ui->exportSheetsCheck->setChecked(cfg.exportSheetsAsXlsx());
}

void SettingsDialog::saveSettings() {
    auto& cfg = ConfigManager::instance();
    cfg.setCacheTtlSeconds(ui->cacheTtlSpin->value());
    const std::wstring lang = (ui->languageCombo->currentIndex() == kIdxLangEn) ? L"en" : L"uk";
    cfg.setLanguage(lang);
    cfg.setLogLevel(ui->logLevelCombo->currentIndex());
    cfg.setExportDocsAsDocx(ui->exportDocsCheck->isChecked());
    cfg.setExportSheetsAsXlsx(ui->exportSheetsCheck->isChecked());
    cfg.save();

    FilesystemBridge::instance().setCacheTtl(ui->cacheTtlSpin->value());
    GUIManager::instance().applyLanguage(lang);

    LogLevel newLevel;
    switch (ui->logLevelCombo->currentIndex()) {
        case 0:  newLevel = LogLevel::Debug; break;
        case 2:  newLevel = LogLevel::Error; break;
        default: newLevel = LogLevel::Info;  break;
    }
    Logger::instance().setLevel(newLevel);
}

void SettingsDialog::onAddAccount() {
    if (!m_oauth) {
        Logger::instance().warn("SettingsDialog: no OAuthManager — cannot add account");
        return;
    }

    AuthDialog dlg(m_oauth,
                   GUIManager::instance().oauthClientId(),
                   GUIManager::instance().oauthClientSecret(),
                   this);
    if (dlg.exec() == QDialog::Accepted
        && ConfigManager::instance().defaultAccount().empty()
        && !dlg.accountEmail().isEmpty()) {
        ConfigManager::instance().setDefaultAccount(dlg.accountEmail().toStdWString());
        ConfigManager::instance().save();
    }
    reloadAccounts();
}

void SettingsDialog::onRemoveAccount() {
    const QString email = currentAccountEmail();
    if (email.isEmpty() || !m_oauth) return;

    const bool wasDefault =
        ConfigManager::instance().defaultAccount() == email.toStdWString();

    m_oauth->logout(email.toStdString());

    if (wasDefault) {
        ConfigManager::instance().setDefaultAccount(L"");
        ConfigManager::instance().save();
    }

    FilesystemBridge::instance().onAccountChanged();
    reloadAccounts();
}

void SettingsDialog::onSetDefaultAccount() {
    const QString email = currentAccountEmail();
    if (email.isEmpty()) return;
    ConfigManager::instance().setDefaultAccount(email.toStdWString());
    ConfigManager::instance().save();
    FilesystemBridge::instance().onAccountChanged();
    reloadAccounts();
}

void SettingsDialog::changeEvent(QEvent* e) {
    QDialog::changeEvent(e);
    if (e->type() == QEvent::LanguageChange)
        ui->retranslateUi(this);
}

void SettingsDialog::onAccepted() {
    saveSettings();
    accept();
}

#include "moc_settings_dialog.cpp"
