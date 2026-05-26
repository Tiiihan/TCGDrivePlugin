#pragma once

#include <QString>

#include <cstdint>
#include <mutex>
#include <set>
#include <string>

class QApplication;
class QTranslator;
class OAuthManager;
class ProgressDialog;

class GUIManager {
public:
    static GUIManager& instance();

    QString oauthClientId() const;
    QString oauthClientSecret() const;

    void setOAuthManager(OAuthManager* oauth);

    void showAuthDialog(OAuthManager* oauth);
    void showSettings();

    void applyLanguage(const std::wstring& lang);

    ProgressDialog* createProgressDialog(const QString& filename, std::int64_t totalSize);
    void            updateProgressDialog(ProgressDialog* dlg, qint64 transferred, qint64 total);
    void            destroyProgressDialog(ProgressDialog* dlg);

    bool isProgressDialogCancelled(ProgressDialog* dlg);

    void ensureInitialized();

private:
    GUIManager();
    ~GUIManager();
    GUIManager(const GUIManager&)            = delete;
    GUIManager& operator=(const GUIManager&) = delete;

    bool ensureApp();      
    bool onGuiThread();     

    mutable std::mutex m_mutex;

    QApplication* m_app        = nullptr;
    bool          m_ownsApp   = false;
    QTranslator*  m_translator = nullptr;

    OAuthManager* m_oauth   = nullptr;

    std::set<ProgressDialog*> m_progressDialogs;
};
