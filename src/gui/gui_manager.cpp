#include "gui_manager.h"

#include "auth_dialog.h"
#include "progress_dialog.h"
#include "settings_dialog.h"
#include "../storage/config_manager.h"
#include "../utils/logger.h"

#include <QApplication>
#include <QDialog>
#include <QMetaObject>
#include <QThread>
#include <QTranslator>

namespace {

int   g_argc = 1;
char  g_arg0[] = "TCGDrivePlugin";
char* g_argv[] = { g_arg0, nullptr };

} // namespace

GUIManager& GUIManager::instance() {
    static GUIManager s_instance;
    return s_instance;
}

GUIManager::GUIManager()  = default;

GUIManager::~GUIManager() {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (ProgressDialog* dlg : m_progressDialogs) {
        delete dlg;
    }
    m_progressDialogs.clear();
}

QString GUIManager::oauthClientId() const {
    return QString::fromStdWString(ConfigManager::instance().clientId());
}

QString GUIManager::oauthClientSecret() const {
    return QString::fromStdWString(ConfigManager::instance().clientSecret());
}

void GUIManager::setOAuthManager(OAuthManager* oauth) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_oauth = oauth;
}

bool GUIManager::ensureApp() {
    bool justCreated = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_app) return true;
        if (QApplication* existing = qobject_cast<QApplication*>(QCoreApplication::instance())) {
            m_app     = existing;
            m_ownsApp = false;
            return true;
        }
        m_app     = new QApplication(g_argc, g_argv);
        m_ownsApp = true;
        justCreated = true;
        Logger::instance().info("GUIManager: created QApplication");
    }
    if (justCreated) {
        applyLanguage(ConfigManager::instance().language());
    }
    return m_app != nullptr;
}

bool GUIManager::onGuiThread() {
    if (!ensureApp()) return false;
    return QThread::currentThread() == m_app->thread();
}

void GUIManager::showAuthDialog(OAuthManager* oauth) {
    setOAuthManager(oauth);
    if (!onGuiThread()) {
        Logger::instance().warn("GUIManager::showAuthDialog called off the GUI thread; ignored. "
                                "Open the Google Drive folder in Total Commander to sign in.");
        return;
    }
    const QString cid  = oauthClientId();
    const QString csec = oauthClientSecret();
    if (cid.isEmpty() || csec.isEmpty()) {
        Logger::instance().error("GUIManager::showAuthDialog: client_id/client_secret not "
                                 "configured (set them in %APPDATA%\\TCGDrivePlugin\\config.ini)");
    }

    AuthDialog dlg(oauth, cid, csec);
    QObject::connect(&dlg, &AuthDialog::authCompleted, [](bool ok, const QString& email) {
        if (ok && !email.isEmpty()) {
            Logger::instance().info("GUIManager: signed in as " + email.toStdString());
            if (ConfigManager::instance().defaultAccount().empty()) {
                ConfigManager::instance().setDefaultAccount(email.toStdWString());
                ConfigManager::instance().save();
            }
        } else {
            Logger::instance().warn("GUIManager: sign-in failed or cancelled");
        }
    });
    dlg.exec();
}

void GUIManager::showSettings() {
    if (!onGuiThread()) {
        Logger::instance().warn("GUIManager::showSettings called off the GUI thread; ignored");
        return;
    }
    OAuthManager* oauth = nullptr;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        oauth = m_oauth;
    }
    if (!oauth) {
        Logger::instance().warn("GUIManager::showSettings: no OAuthManager bound; "
                                "the account list will be empty");
    }
    SettingsDialog dlg(oauth);
    dlg.exec();
}

ProgressDialog* GUIManager::createProgressDialog(const QString& filename, std::int64_t totalSize) {
    if (!onGuiThread()) {
        Logger::instance().debug("GUIManager::createProgressDialog off the GUI thread; "
                                 "no Qt progress dialog (Total Commander's own progress UI is used)");
        return nullptr;
    }
    auto* dlg = new ProgressDialog(filename, static_cast<qint64>(totalSize));
    QObject::connect(dlg, &QDialog::finished, dlg, [this, dlg](int) { destroyProgressDialog(dlg); });
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_progressDialogs.insert(dlg);
    }
    dlg->show();
    return dlg;
}

void GUIManager::updateProgressDialog(ProgressDialog* dlg, qint64 transferred, qint64 total) {
    if (!dlg || !ensureApp()) return;
    if (QThread::currentThread() == m_app->thread()) {
        dlg->updateProgress(transferred, total);
        return;
    }
    QMetaObject::invokeMethod(dlg, [dlg, transferred, total]() {
        dlg->updateProgress(transferred, total);
    }, Qt::QueuedConnection);
}

void GUIManager::destroyProgressDialog(ProgressDialog* dlg) {
    if (!dlg || !ensureApp()) return;

    auto doIt = [this, dlg]() {
        bool claimed = false;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            claimed = (m_progressDialogs.erase(dlg) > 0);
        }
        if (claimed) {
            dlg->close();
            dlg->deleteLater();
        }
    };

    if (QThread::currentThread() == m_app->thread()) {
        doIt();
        return;
    }
    QMetaObject::invokeMethod(m_app, doIt, Qt::QueuedConnection);
}

bool GUIManager::isProgressDialogCancelled(ProgressDialog* dlg) {
    if (!dlg) return false;
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_progressDialogs.find(dlg) == m_progressDialogs.end()) return false;
    return dlg->isCancelled();
}

void GUIManager::ensureInitialized() {
    ensureApp();
}

void GUIManager::applyLanguage(const std::wstring& lang) {
    if (m_translator) {
        QCoreApplication::removeTranslator(m_translator);
        delete m_translator;
        m_translator = nullptr;
    }

    if (lang != L"uk") return;

    m_translator = new QTranslator;
    if (!m_translator->load(QStringLiteral(":/translations/plugin_uk.qm"))) {
        Logger::instance().warn("GUIManager: Ukrainian translation file not found in resources");
        delete m_translator;
        m_translator = nullptr;
        return;
    }
    QCoreApplication::installTranslator(m_translator);
    Logger::instance().info("GUIManager: Ukrainian UI active");
}
