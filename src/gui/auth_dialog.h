#pragma once

#include <QDialog>

#include <atomic>
#include <memory>
#include <string>
#include <thread>

class QLabel;
class QProgressBar;
class QPushButton;
class QTimer;
class OAuthManager;

struct AuthWorkerState; 

class AuthDialog : public QDialog {
    Q_OBJECT

public:
    AuthDialog(OAuthManager*  oauth,
               const QString& clientId,
               const QString& clientSecret,
               QWidget*       parent = nullptr);
    ~AuthDialog() override;

    QString accountEmail() const { return m_accountEmail; }

signals:
    void authCompleted(bool success, QString accountEmail);

private slots:
    void onSignInClicked();
    void onPollTimer();
    void onCancelClicked();

private:
    void buildUi();
    void startWorker();

    OAuthManager* m_oauth = nullptr;
    QString       m_clientId;
    QString       m_clientSecret;
    QString       m_accountEmail;

    QProgressBar* m_busyBar     = nullptr;
    QLabel*       m_statusLabel = nullptr;
    QPushButton*  m_cancelBtn   = nullptr;
    QTimer*       m_pollTimer   = nullptr;

    std::thread                      m_worker;
    std::shared_ptr<AuthWorkerState> m_shared;

    int                m_elapsedMs = 0;
    bool               m_started = false;
    static constexpr int kPollIntervalMs = 400;
    static constexpr int kTimeoutMs      = 120 * 1000;
};
