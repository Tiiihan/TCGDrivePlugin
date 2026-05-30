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

#include <windows.h>

#include <string>

namespace {

int   g_argc = 1;
char  g_arg0[] = "TCGDrivePlugin";
char* g_argv[] = { g_arg0, nullptr };

// Directory of THIS plugin DLL (e.g. <TC>\plugins\wfx\TCGDrivePlugin),
// resolved from an address inside the DLL — NOT the host TOTALCMD64.exe.
std::wstring thisPluginDirectory() {
    HMODULE hmod = nullptr;
    if (!::GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&thisPluginDirectory),
            &hmod)) {
        return {};
    }
    wchar_t path[MAX_PATH] = {};
    const DWORD n = ::GetModuleFileNameW(hmod, path, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return {};
    std::wstring full(path, n);
    const std::size_t slash = full.find_last_of(L"\\/");
    return (slash == std::wstring::npos) ? std::wstring{} : full.substr(0, slash);
}

// Point Qt at the plugins shipped beside this DLL. Qt searches relative to
// the host application (TOTALCMD64.exe) by default, so without this it can
// never find platforms\qwindows.dll and aborts with
// "no Qt platform plugin could be initialized". Must run BEFORE QApplication.
void pinQtPluginPathToThisDll() {
    const std::wstring dir = thisPluginDirectory();
    if (dir.empty()) return;
    const QString qdir = QString::fromStdWString(dir);
    // Platform plugin is located very early via this env var.
    ::qputenv("QT_QPA_PLATFORM_PLUGIN_PATH",
              QString(qdir + "/platforms").toLocal8Bit());
    // Other plugin categories (imageformats, iconengines, styles, tls)
    // resolve through the library paths.
    QCoreApplication::addLibraryPath(qdir);
}

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
        pinQtPluginPathToThisDll();   // must precede QApplication construction
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
