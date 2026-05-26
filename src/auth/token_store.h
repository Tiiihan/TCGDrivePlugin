#pragma once

#include "oauth_manager.h"

#include <mutex>
#include <string>
#include <vector>

class TokenStore {
public:
    TokenStore();
    ~TokenStore();

    bool save  (const std::string& accountId, const OAuthTokens& tokens);
    bool load  (const std::string& accountId, OAuthTokens&       tokens);
    bool remove(const std::string& accountId);

    std::vector<std::string> listAccounts();

private:
    std::wstring storagePath() const;
    bool         readEnvelope (std::string& outJson);
    bool         writeEnvelope(const std::string& json);

    mutable std::mutex m_mutex;
};
