#include "auth_dialog.h"

#include "../auth/oauth_manager.h"
#include "../utils/logger.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

#include <mutex>

struct AuthWorkerState {
    std::atomic<int> state{0};   // 0 = running, 1 = success, 2 = failure
    std::mutex       emailMutex;
    std::string      email;      // valid once state == 1
};

AuthDialog::AuthDialog(OAuthManager*  oauth,
                       const QString& clientId,
                       const QString& clientSecret,
                       QWidget*       parent)
    : QDialog(parent),
      m_oauth(oauth),
      m_clientId(clientId),
      m_clientSecret(clientSecret),
      m_shared(std::make_shared<AuthWorkerState>()) {
    buildUi();

    m_pollTimer = new QTimer(this);
    m_pollTimer->setInterval(kPollIntervalMs);
    connect(m_pollTimer, &QTimer::timeout, this, &AuthDialog::onPollTimer);

    QTimer::singleShot(0, this, &AuthDialog::onSignInClicked);
}

AuthDialog::~AuthDialog() {
    if (m_pollTimer) m_pollTimer->stop();
    if (m_worker.joinable()) {
        if (m_oauth) m_oauth->cancelAuthentication();
        m_worker.join();
    }
}

void AuthDialog::buildUi() {
    setWindowTitle(tr("Google Drive authorization"));
    setModal(true);
    setMinimumWidth(440);

    auto* layout = new QVBoxLayout(this);

    auto* info = new QLabel(
        tr("A secure Google sign-in window is opening. "
           "Please complete the authorization in that window."), this);
    info->setWordWrap(true);
    layout->addWidget(info);

    m_statusLabel = new QLabel(
        tr("Waiting for you to finish signing in — this dialog will close automatically."),
        this);
    m_statusLabel->setWordWrap(true);
    layout->addWidget(m_statusLabel);

    m_busyBar = new QProgressBar(this);
    m_busyBar->setRange(0, 0);          
    m_busyBar->setTextVisible(false);
    layout->addWidget(m_busyBar);

    auto* row = new QHBoxLayout();
    row->addStretch();
    m_cancelBtn = new QPushButton(tr("Cancel"), this);
    row->addWidget(m_cancelBtn);
    layout->addLayout(row);

    connect(m_cancelBtn, &QPushButton::clicked, this, &AuthDialog::onCancelClicked);
}

void AuthDialog::onSignInClicked() {
    if (m_started) return;
    m_started = true;
    m_elapsedMs = 0;
    startWorker();
    m_pollTimer->start();
}

void AuthDialog::startWorker() {
    auto              shared = m_shared;
    OAuthManager*     oauth  = m_oauth;
    const std::string cid    = m_clientId.toStdString();
    const std::string csec   = m_clientSecret.toStdString();

    m_worker = std::thread([shared, oauth, cid, csec]() {
        bool        ok = false;
        std::string email;
        try {
            if (oauth) {
                ok = oauth->authenticate(cid, csec);
                if (ok) email = oauth->lastAuthenticatedAccount();
            }
        } catch (const std::exception& e) {
            Logger::instance().error(std::string("AuthDialog worker: ") + e.what());
            ok = false;
        } catch (...) {
            ok = false;
        }
        if (ok) {
            std::lock_guard<std::mutex> lock(shared->emailMutex);
            shared->email = email;
        }
        shared->state.store(ok ? 1 : 2);
    });
}

void AuthDialog::onPollTimer() {
    m_elapsedMs += kPollIntervalMs;

    const int st = m_shared->state.load();
    if (st == 1) {
        {
            std::lock_guard<std::mutex> lock(m_shared->emailMutex);
            m_accountEmail = QString::fromStdString(m_shared->email);
        }
        m_pollTimer->stop();
        Logger::instance().info("AuthDialog: success for "
                                + m_accountEmail.toStdString());
        emit authCompleted(true, m_accountEmail);
        accept();
        return;
    }
    if (st == 2) {
        m_pollTimer->stop();
        Logger::instance().warn("AuthDialog: authentication failed");
        emit authCompleted(false, QString());
        reject();
        return;
    }
    if (m_elapsedMs >= kTimeoutMs) {
        m_pollTimer->stop();
        Logger::instance().warn("AuthDialog: timed out waiting for browser callback");
        emit authCompleted(false, QString());
        reject();
        return;
    }
}

void AuthDialog::onCancelClicked() {
    if (m_pollTimer) m_pollTimer->stop();
    Logger::instance().info("AuthDialog: cancelled by user");
    if (m_oauth) m_oauth->cancelAuthentication();
    emit authCompleted(false, QString());
    reject();
}
