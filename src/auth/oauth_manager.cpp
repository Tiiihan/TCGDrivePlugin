#include "oauth_manager.h"

#include "token_store.h"
#include "webview2_auth_window.h"
#include "../storage/config_manager.h"
#include "../utils/logger.h"
#include "../utils/string_utils.h"

#include <windows.h>
#include <objbase.h>       
#include <wincrypt.h>

#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <openssl/evp.h>
#include <openssl/rand.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <sstream>

#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "ole32.lib")

namespace {

using nlohmann::json;

constexpr const char* kAuthEndpoint     = "https://accounts.google.com/o/oauth2/v2/auth";
constexpr const char* kTokenEndpoint    = "https://oauth2.googleapis.com/token";
constexpr const char* kUserInfoEndpoint = "https://openidconnect.googleapis.com/v1/userinfo";
constexpr const char* kRevokeEndpoint   = "https://oauth2.googleapis.com/revoke";
constexpr const char* kScope            = "openid email https://www.googleapis.com/auth/drive";
constexpr std::uint16_t kRedirectPort   = 9876;
constexpr int           kCallbackTimeoutSec = 120;
constexpr int           kRefreshSkewSec     = 60;

void initCurlOnce() {
    static std::once_flag flag;
    std::call_once(flag, []() {
        ::curl_global_init(CURL_GLOBAL_DEFAULT);
        Logger::instance().info("libcurl initialised");
    });
}


size_t curlWriteCb(void* contents, size_t size, size_t nmemb, void* userp) {
    auto* out = static_cast<std::string*>(userp);
    out->append(static_cast<char*>(contents), size * nmemb);
    return size * nmemb;
}

struct HttpReply {
    long        status = 0;
    std::string body;
    std::string error; 
};

HttpReply httpPost(const std::string&              url,
                   const std::string&              body,
                   const std::vector<std::string>& headers) {
    HttpReply reply;
    initCurlOnce();

    CURL* curl = ::curl_easy_init();
    if (!curl) { reply.error = "curl_easy_init failed"; return reply; }

    curl_slist* hdr = nullptr;
    for (auto& h : headers) hdr = ::curl_slist_append(hdr, h.c_str());

    ::curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    ::curl_easy_setopt(curl, CURLOPT_POST, 1L);
    ::curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.data());
    ::curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
    if (hdr) ::curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdr);
    ::curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteCb);
    ::curl_easy_setopt(curl, CURLOPT_WRITEDATA, &reply.body);
    ::curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    ::curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    ::curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    ::curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

    CURLcode rc = ::curl_easy_perform(curl);
    if (rc != CURLE_OK) {
        reply.error = ::curl_easy_strerror(rc);
    } else {
        ::curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &reply.status);
    }
    if (hdr) ::curl_slist_free_all(hdr);
    ::curl_easy_cleanup(curl);
    return reply;
}

HttpReply httpGet(const std::string&              url,
                  const std::vector<std::string>& headers) {
    HttpReply reply;
    initCurlOnce();

    CURL* curl = ::curl_easy_init();
    if (!curl) { reply.error = "curl_easy_init failed"; return reply; }

    curl_slist* hdr = nullptr;
    for (auto& h : headers) hdr = ::curl_slist_append(hdr, h.c_str());

    ::curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    ::curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    if (hdr) ::curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdr);
    ::curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteCb);
    ::curl_easy_setopt(curl, CURLOPT_WRITEDATA, &reply.body);
    ::curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    ::curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    ::curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    ::curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

    CURLcode rc = ::curl_easy_perform(curl);
    if (rc != CURLE_OK) {
        reply.error = ::curl_easy_strerror(rc);
    } else {
        ::curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &reply.status);
    }
    if (hdr) ::curl_slist_free_all(hdr);
    ::curl_easy_cleanup(curl);
    return reply;
}


std::string urlEncode(const std::string& s) {
    return StringUtils::urlEncode(s);
}

std::string buildForm(const std::vector<std::pair<std::string, std::string>>& kv) {
    std::string out;
    for (auto& p : kv) {
        if (!out.empty()) out += '&';
        out += urlEncode(p.first);
        out += '=';
        out += urlEncode(p.second);
    }
    return out;
}

std::string base64UrlEncode(const std::uint8_t* data, std::size_t len) {
    DWORD sz = 0;
    ::CryptBinaryToStringA(data, static_cast<DWORD>(len),
                           CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF,
                           nullptr, &sz);
    if (sz == 0) return {};
    std::string s(sz, '\0');
    if (!::CryptBinaryToStringA(data, static_cast<DWORD>(len),
                                CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF,
                                s.data(), &sz)) return {};
    s.resize(sz);
    for (auto& c : s) {
        if (c == '+') c = '-';
        else if (c == '/') c = '_';
    }
    while (!s.empty() && s.back() == '=') s.pop_back();
    return s;
}

std::string generateCodeVerifier() {
    std::array<std::uint8_t, 32> buf{};
    if (::RAND_bytes(buf.data(), static_cast<int>(buf.size())) != 1) {
        Logger::instance().error("RAND_bytes failed");
        return {};
    }
    return base64UrlEncode(buf.data(), buf.size());
}

std::string computeCodeChallenge(const std::string& verifier) {
    std::array<std::uint8_t, 32> digest{};
    unsigned int len = 0;
    EVP_MD_CTX* ctx = ::EVP_MD_CTX_new();
    if (!ctx) return {};
    bool ok = ::EVP_DigestInit_ex(ctx, ::EVP_sha256(), nullptr) == 1
           && ::EVP_DigestUpdate(ctx, verifier.data(), verifier.size()) == 1
           && ::EVP_DigestFinal_ex(ctx, digest.data(), &len) == 1;
    ::EVP_MD_CTX_free(ctx);
    if (!ok || len != digest.size()) return {};
    return base64UrlEncode(digest.data(), digest.size());
}

std::string generateState() {
    std::array<std::uint8_t, 16> buf{};
    if (::RAND_bytes(buf.data(), static_cast<int>(buf.size())) != 1) return {};
    return base64UrlEncode(buf.data(), buf.size());
}

} // namespace

// =========================================================================

OAuthManager::OAuthManager()
    : m_store(std::make_unique<TokenStore>()) {
    m_clientId     = StringUtils::toUtf8(ConfigManager::instance().clientId());
    m_clientSecret = StringUtils::toUtf8(ConfigManager::instance().clientSecret());
}

OAuthManager::~OAuthManager() = default;

bool OAuthManager::authenticate(const std::string& clientId,
                                const std::string& clientSecret) {
    if (clientId.empty() || clientSecret.empty()) {
        Logger::instance().error("authenticate: empty client credentials");
        return false;
    }
    m_cancelRequested.store(false);  
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_clientId     = clientId;
        m_clientSecret = clientSecret;
    }

    // --- 1. PKCE
    const std::string verifier  = generateCodeVerifier();
    const std::string challenge = computeCodeChallenge(verifier);
    const std::string state     = generateState();
    if (verifier.empty() || challenge.empty() || state.empty()) {
        Logger::instance().error("authenticate: PKCE/state generation failed");
        return false;
    }

    const std::string redirectUri =
        "http://127.0.0.1:" + std::to_string(kRedirectPort);

    std::string authUrl = kAuthEndpoint;
    authUrl += "?" + buildForm({
        {"client_id",             clientId},
        {"redirect_uri",          redirectUri},
        {"response_type",         "code"},
        {"scope",                 kScope},
        {"code_challenge",        challenge},
        {"code_challenge_method", "S256"},
        {"state",                 state},
        {"access_type",           "offline"},
        {"prompt",                "select_account consent"},
    });

    Logger::instance().info("authenticate: showing embedded sign-in window");
    const HRESULT comHr = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(comHr)) {
        Logger::instance().error("authenticate: CoInitializeEx failed (hr=0x"
                                 + [comHr]{
                                     char b[12];
                                     std::snprintf(b, sizeof(b), "%08lX",
                                         static_cast<unsigned long>(comHr));
                                     return std::string(b);
                                 }() + ")");
        return false;
    }

    if (m_cancelRequested.load()) {
        ::CoUninitialize();
        Logger::instance().info("authenticate: cancelled before the sign-in window opened");
        return false;
    }

    const WebView2Result wvResult =
        WebView2AuthWindow::show(authUrl, redirectUri, kCallbackTimeoutSec,
                                 &m_cancelRequested);
    ::CoUninitialize();

    if (wvResult.timedOut) {
        Logger::instance().error("authenticate: sign-in window timed out");
        return false;
    }
    if (!wvResult.success) {
        const std::string reason =
            wvResult.error.empty() ? "user_cancelled" : wvResult.error;
        Logger::instance().error("authenticate: sign-in failed: " + reason);
        return false;
    }
    if (wvResult.state != state) {
        Logger::instance().error("authenticate: state mismatch (CSRF guard)");
        return false;
    }

    OAuthTokens tokens;
    if (!exchangeCodeForTokens(clientId, clientSecret, wvResult.code, verifier,
                               redirectUri, tokens)) {
        Logger::instance().error("authenticate: token exchange failed");
        return false;
    }

    std::string email;
    if (!fetchUserEmail(tokens.accessToken, email) || email.empty()) {
        Logger::instance().error("authenticate: failed to fetch user email");
        return false;
    }

    if (!m_store->save(email, tokens)) {
        Logger::instance().error("authenticate: token persistence failed");
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_lastEmail            = email;
        m_tokenCache[email]    = tokens; 
    }
    Logger::instance().info("authenticate: signed in as " + email);
    return true;
}

std::string OAuthManager::getAccessToken(const std::string& accountId) {
    if (accountId.empty()) return {};

    OAuthTokens tokens;
    bool fromCache = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_tokenCache.find(accountId);
        if (it != m_tokenCache.end()) {
            tokens    = it->second;
            fromCache = true;
        }
    }
    if (!fromCache) {
        if (!m_store->load(accountId, tokens)) {
            Logger::instance().warn("getAccessToken: no tokens for " + accountId);
            return {};
        }
    }

    const auto now  = std::chrono::system_clock::now();
    const auto skew = std::chrono::seconds(kRefreshSkewSec);
    if (tokens.expiresAt - skew > now && !tokens.accessToken.empty()) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_tokenCache[accountId] = tokens;
        return tokens.accessToken;
    }

    std::string clientId, clientSecret;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        clientId     = m_clientId;
        clientSecret = m_clientSecret;
    }
    if (clientId.empty() || clientSecret.empty()) {
        Logger::instance().error("getAccessToken: OAuth client credentials unavailable");
        return {};
    }
    if (tokens.refreshToken.empty()) {
        Logger::instance().error("getAccessToken: no refresh token for " + accountId
                                 + " — re-authentication required");
        return {};
    }

    if (!refreshAccessToken(clientId, clientSecret, tokens)) {
        Logger::instance().error("getAccessToken: refresh failed for " + accountId);
        std::lock_guard<std::mutex> lock(m_mutex);
        m_tokenCache.erase(accountId);
        return {};
    }
    if (!m_store->save(accountId, tokens)) {
        Logger::instance().warn("getAccessToken: refresh succeeded but save failed");
    }
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_tokenCache[accountId] = tokens;
    }
    return tokens.accessToken;
}

void OAuthManager::cancelAuthentication() {
    m_cancelRequested.store(true);
}

void OAuthManager::logout(const std::string& accountId) {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_tokenCache.erase(accountId);
    }
    OAuthTokens t;
    if (m_store->load(accountId, t) && !t.refreshToken.empty()) {
        revokeToken(t.refreshToken);
    }
    m_store->remove(accountId);

    try {
        const std::filesystem::path profileDir =
            WebView2AuthWindow::userDataPath();
        if (std::filesystem::exists(profileDir)) {
            std::filesystem::remove_all(profileDir);
            Logger::instance().info("logout: WebView2 profile cleared");
        }
    } catch (const std::exception& e) {
        Logger::instance().warn(std::string("logout: could not clear WebView2 profile: ")
                                + e.what());
    }

    Logger::instance().info("logout: removed " + accountId);
}

std::vector<std::string> OAuthManager::getAccountList() {
    return m_store->listAccounts();
}

std::string OAuthManager::lastAuthenticatedAccount() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_lastEmail;
}

bool OAuthManager::exchangeCodeForTokens(const std::string& clientId,
                                         const std::string& clientSecret,
                                         const std::string& code,
                                         const std::string& codeVerifier,
                                         const std::string& redirectUri,
                                         OAuthTokens&       outTokens) {
    const std::string body = buildForm({
        {"grant_type",    "authorization_code"},
        {"code",          code},
        {"code_verifier", codeVerifier},
        {"client_id",     clientId},
        {"client_secret", clientSecret},
        {"redirect_uri",  redirectUri},
    });

    HttpReply rep = httpPost(kTokenEndpoint, body,
                             {"Content-Type: application/x-www-form-urlencoded"});

    if (!rep.error.empty()) {
        Logger::instance().error("token exchange transport error: " + rep.error);
        return false;
    }
    if (rep.status != 200) {
        Logger::instance().error("token exchange HTTP " + std::to_string(rep.status)
                                 + " body=" + rep.body);
        return false;
    }

    try {
        json j = json::parse(rep.body);
        outTokens.accessToken  = j.value("access_token",  std::string{});
        outTokens.refreshToken = j.value("refresh_token", std::string{});
        outTokens.tokenType    = j.value("token_type",    std::string{"Bearer"});
        outTokens.scope        = j.value("scope",         std::string{kScope});
        int expiresIn          = j.value("expires_in",    3600);
        outTokens.expiresAt    = std::chrono::system_clock::now()
                               + std::chrono::seconds(expiresIn);

        if (outTokens.accessToken.empty()) {
            Logger::instance().error("token exchange: missing access_token in response");
            return false;
        }
        return true;
    } catch (const std::exception& e) {
        Logger::instance().error(std::string("token exchange parse: ") + e.what());
        return false;
    }
}

bool OAuthManager::refreshAccessToken(const std::string& clientId,
                                      const std::string& clientSecret,
                                      OAuthTokens&       tokens) {
    if (tokens.refreshToken.empty()) return false;

    const std::string body = buildForm({
        {"grant_type",    "refresh_token"},
        {"client_id",     clientId},
        {"client_secret", clientSecret},
        {"refresh_token", tokens.refreshToken},
    });

    HttpReply rep = httpPost(kTokenEndpoint, body,
                             {"Content-Type: application/x-www-form-urlencoded"});
    if (!rep.error.empty()) {
        Logger::instance().error("refresh transport error: " + rep.error);
        return false;
    }
    if (rep.status == 400 || rep.status == 401) {
        Logger::instance().error("refresh: token revoked/expired (HTTP "
                                 + std::to_string(rep.status) + ")");
        return false;
    }
    if (rep.status != 200) {
        Logger::instance().error("refresh HTTP " + std::to_string(rep.status)
                                 + " body=" + rep.body);
        return false;
    }

    try {
        json j = json::parse(rep.body);
        tokens.accessToken = j.value("access_token", std::string{});
        if (j.contains("refresh_token") && j["refresh_token"].is_string()) {
            tokens.refreshToken = j["refresh_token"].get<std::string>();
        }
        if (j.contains("token_type")) tokens.tokenType = j["token_type"].get<std::string>();
        if (j.contains("scope"))      tokens.scope     = j["scope"].get<std::string>();
        int expiresIn = j.value("expires_in", 3600);
        tokens.expiresAt = std::chrono::system_clock::now()
                         + std::chrono::seconds(expiresIn);
        return !tokens.accessToken.empty();
    } catch (const std::exception& e) {
        Logger::instance().error(std::string("refresh parse: ") + e.what());
        return false;
    }
}

bool OAuthManager::fetchUserEmail(const std::string& accessToken,
                                  std::string&       outEmail) {
    HttpReply rep = httpGet(kUserInfoEndpoint,
                            {"Authorization: Bearer " + accessToken});
    if (!rep.error.empty()) {
        Logger::instance().error("userinfo transport: " + rep.error);
        return false;
    }
    if (rep.status != 200) {
        Logger::instance().error("userinfo HTTP " + std::to_string(rep.status));
        return false;
    }
    try {
        json j = json::parse(rep.body);
        outEmail = j.value("email", std::string{});
        return !outEmail.empty();
    } catch (const std::exception& e) {
        Logger::instance().error(std::string("userinfo parse: ") + e.what());
        return false;
    }
}

bool OAuthManager::revokeToken(const std::string& refreshToken) {
    const std::string body = buildForm({{"token", refreshToken}});
    HttpReply rep = httpPost(kRevokeEndpoint, body,
                             {"Content-Type: application/x-www-form-urlencoded"});
    if (!rep.error.empty()) {
        Logger::instance().warn("revoke transport: " + rep.error);
        return false;
    }
    if (rep.status != 200) {
        Logger::instance().warn("revoke HTTP " + std::to_string(rep.status));
    }
    return rep.status == 200;
}
