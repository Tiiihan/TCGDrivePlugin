#include "token_store.h"

#include "../utils/logger.h"

#include <windows.h>
#include <wincrypt.h>
#include <dpapi.h>
#include <shlobj.h>

#include <nlohmann/json.hpp>

#include <cstdio>
#include <fstream>
#include <sstream>
#include <vector>

#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "shell32.lib")

namespace {

using nlohmann::json;

bool dpapiEncrypt(const std::vector<std::uint8_t>& plain,
                  std::vector<std::uint8_t>&       cipher) {
    DATA_BLOB in{};
    DATA_BLOB out{};
    in.pbData = const_cast<BYTE*>(plain.data());
    in.cbData = static_cast<DWORD>(plain.size());

    if (!::CryptProtectData(&in,
                            L"TCGDrivePlugin",
                            /*pOptionalEntropy=*/nullptr,
                            /*pvReserved=*/      nullptr,
                            /*pPromptStruct=*/   nullptr,
                            CRYPTPROTECT_UI_FORBIDDEN,
                            &out)) {
        Logger::instance().error("CryptProtectData failed: "
                                 + std::to_string(::GetLastError()));
        return false;
    }
    cipher.assign(out.pbData, out.pbData + out.cbData);
    ::LocalFree(out.pbData);
    return true;
}

bool dpapiDecrypt(const std::vector<std::uint8_t>& cipher,
                  std::vector<std::uint8_t>&       plain) {
    DATA_BLOB in{};
    DATA_BLOB out{};
    in.pbData = const_cast<BYTE*>(cipher.data());
    in.cbData = static_cast<DWORD>(cipher.size());

    LPWSTR desc = nullptr;
    if (!::CryptUnprotectData(&in,
                              &desc,
                              /*pOptionalEntropy=*/nullptr,
                              /*pvReserved=*/      nullptr,
                              /*pPromptStruct=*/   nullptr,
                              CRYPTPROTECT_UI_FORBIDDEN,
                              &out)) {
        Logger::instance().error("CryptUnprotectData failed: "
                                 + std::to_string(::GetLastError()));
        return false;
    }
    plain.assign(out.pbData, out.pbData + out.cbData);
    if (desc) ::LocalFree(desc);
    ::LocalFree(out.pbData);
    return true;
}

std::string base64Encode(const std::vector<std::uint8_t>& data) {
    DWORD sz = 0;
    ::CryptBinaryToStringA(data.data(), static_cast<DWORD>(data.size()),
                           CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF,
                           nullptr, &sz);
    if (sz == 0) return {};
    std::string out(sz, '\0');
    if (!::CryptBinaryToStringA(data.data(), static_cast<DWORD>(data.size()),
                                CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF,
                                out.data(), &sz)) {
        return {};
    }
    out.resize(sz);
    return out;
}

std::vector<std::uint8_t> base64Decode(const std::string& text) {
    DWORD sz = 0;
    if (!::CryptStringToBinaryA(text.c_str(), 0, CRYPT_STRING_BASE64,
                                nullptr, &sz, nullptr, nullptr)) {
        return {};
    }
    std::vector<std::uint8_t> out(sz);
    if (!::CryptStringToBinaryA(text.c_str(), 0, CRYPT_STRING_BASE64,
                                out.data(), &sz, nullptr, nullptr)) {
        return {};
    }
    out.resize(sz);
    return out;
}

std::int64_t toUnixSeconds(std::chrono::system_clock::time_point tp) {
    return std::chrono::duration_cast<std::chrono::seconds>(
               tp.time_since_epoch()).count();
}

std::chrono::system_clock::time_point fromUnixSeconds(std::int64_t s) {
    return std::chrono::system_clock::time_point(std::chrono::seconds(s));
}

std::string tokensToJson(const OAuthTokens& t, const std::string& email) {
    json j;
    j["access_token"]  = t.accessToken;
    j["refresh_token"] = t.refreshToken;
    j["token_type"]    = t.tokenType;
    j["scope"]         = t.scope;
    j["expires_at"]    = toUnixSeconds(t.expiresAt);
    j["email"]         = email;
    return j.dump();
}

bool tokensFromJson(const std::string& text, OAuthTokens& t) {
    try {
        json j = json::parse(text);
        t.accessToken  = j.value("access_token",  std::string{});
        t.refreshToken = j.value("refresh_token", std::string{});
        t.tokenType    = j.value("token_type",    std::string{});
        t.scope        = j.value("scope",         std::string{});
        t.expiresAt    = fromUnixSeconds(j.value("expires_at", std::int64_t{0}));
        return !t.accessToken.empty();
    } catch (const std::exception& e) {
        Logger::instance().error(std::string("tokensFromJson: ") + e.what());
        return false;
    }
}

}

TokenStore::TokenStore()  = default;
TokenStore::~TokenStore() = default;

std::wstring TokenStore::storagePath() const {
    PWSTR roaming = nullptr;
    if (FAILED(::SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &roaming))) {
        return L"";
    }
    std::wstring dir = roaming;
    ::CoTaskMemFree(roaming);

    dir += L"\\TCGDrivePlugin";
    ::CreateDirectoryW(dir.c_str(), nullptr); 
    return dir + L"\\tokens.dat";
}

bool TokenStore::readEnvelope(std::string& outJson) {
    const std::wstring path = storagePath();
    if (path.empty()) return false;

    std::ifstream in(path, std::ios::binary);
    if (!in) {
        outJson = "{\"version\":1,\"accounts\":{}}";
        return true;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    outJson = ss.str();
    if (outJson.empty()) outJson = "{\"version\":1,\"accounts\":{}}";
    return true;
}

bool TokenStore::writeEnvelope(const std::string& jsonText) {
    const std::wstring path = storagePath();
    if (path.empty()) return false;

    std::wstring tmp = path + L".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) {
            Logger::instance().error("TokenStore: cannot open temp file for write");
            return false;
        }
        out.write(jsonText.data(), static_cast<std::streamsize>(jsonText.size()));
    }
    if (!::MoveFileExW(tmp.c_str(), path.c_str(),
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        Logger::instance().error("TokenStore: MoveFileExW failed: "
                                 + std::to_string(::GetLastError()));
        ::DeleteFileW(tmp.c_str());
        return false;
    }
    return true;
}

bool TokenStore::save(const std::string& accountId, const OAuthTokens& tokens) {
    if (accountId.empty()) return false;
    std::lock_guard<std::mutex> lock(m_mutex);

    std::string env;
    if (!readEnvelope(env)) return false;

    json doc;
    try {
        doc = json::parse(env);
    } catch (const std::exception& e) {
        Logger::instance().error(std::string("TokenStore::save: refusing to overwrite a "
                                 "corrupt tokens file (would lose all accounts): ")
                                 + e.what());
        return false;
    }

    const std::string plainJson = tokensToJson(tokens, accountId);
    std::vector<std::uint8_t> plainBytes(plainJson.begin(), plainJson.end());

    std::vector<std::uint8_t> cipherBytes;
    if (!dpapiEncrypt(plainBytes, cipherBytes)) return false;

    doc["accounts"][accountId] = {
        { "encrypted", base64Encode(cipherBytes) },
    };

    return writeEnvelope(doc.dump(2));
}

bool TokenStore::load(const std::string& accountId, OAuthTokens& tokens) {
    if (accountId.empty()) return false;
    std::lock_guard<std::mutex> lock(m_mutex);

    std::string env;
    if (!readEnvelope(env)) return false;
    try {
        json doc = json::parse(env);
        if (!doc.contains("accounts")) return false;
        auto& accts = doc["accounts"];
        if (!accts.contains(accountId)) return false;
        const std::string b64 = accts[accountId].value("encrypted", std::string{});
        if (b64.empty()) return false;

        std::vector<std::uint8_t> cipher = base64Decode(b64);
        if (cipher.empty()) return false;
        std::vector<std::uint8_t> plain;
        if (!dpapiDecrypt(cipher, plain)) return false;

        std::string text(plain.begin(), plain.end());
        return tokensFromJson(text, tokens);
    } catch (const std::exception& e) {
        Logger::instance().error(std::string("TokenStore::load: ") + e.what());
        return false;
    }
}

bool TokenStore::remove(const std::string& accountId) {
    if (accountId.empty()) return false;
    std::lock_guard<std::mutex> lock(m_mutex);

    std::string env;
    if (!readEnvelope(env)) return false;
    try {
        json doc = json::parse(env);
        if (!doc.contains("accounts")) return true;
        doc["accounts"].erase(accountId);
        return writeEnvelope(doc.dump(2));
    } catch (const std::exception& e) {
        Logger::instance().error(std::string("TokenStore::remove: ") + e.what());
        return false;
    }
}

std::vector<std::string> TokenStore::listAccounts() {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<std::string> out;

    std::string env;
    if (!readEnvelope(env)) return out;
    try {
        json doc = json::parse(env);
        if (!doc.contains("accounts")) return out;
        for (auto it = doc["accounts"].begin(); it != doc["accounts"].end(); ++it) {
            out.push_back(it.key());
        }
    } catch (const std::exception& e) {
        Logger::instance().error(std::string("TokenStore::listAccounts: ") + e.what());
    }
    return out;
}
