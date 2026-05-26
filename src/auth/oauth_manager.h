#pragma once

#include <atomic>
#include <chrono>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>


struct OAuthTokens {
    std::string accessToken;                            // токен доступу
    std::string refreshToken;                           // токен оновлення
    std::string tokenType;                              // тип (Bearer)
    std::string scope;                                  // область доступу
    std::chrono::system_clock::time_point expiresAt;    // термін дії
};

class TokenStore;

class OAuthManager {
public:
    OAuthManager();
    ~OAuthManager();

    OAuthManager(const OAuthManager&)            = delete;
    OAuthManager& operator=(const OAuthManager&) = delete;

    bool authenticate(const std::string& clientId,
                      const std::string& clientSecret);

    void cancelAuthentication();

    std::string getAccessToken(const std::string& accountId);

    void logout(const std::string& accountId);

    std::vector<std::string> getAccountList();

    std::string lastAuthenticatedAccount() const;

private:
    bool exchangeCodeForTokens(const std::string& clientId,
                               const std::string& clientSecret,
                               const std::string& code,
                               const std::string& codeVerifier,
                               const std::string& redirectUri,
                               OAuthTokens&       outTokens);

    bool refreshAccessToken(const std::string& clientId,
                            const std::string& clientSecret,
                            OAuthTokens&       tokens);

    bool fetchUserEmail(const std::string& accessToken,
                        std::string&       outEmail);

    bool revokeToken(const std::string& refreshToken);

    std::unique_ptr<TokenStore> m_store;
    mutable std::mutex          m_mutex;
    std::string                 m_clientId;
    std::string                 m_clientSecret;
    std::string                 m_lastEmail;

    std::atomic<bool>           m_cancelRequested{false};

    std::map<std::string, OAuthTokens> m_tokenCache;
};
