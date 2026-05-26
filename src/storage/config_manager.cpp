#include "config_manager.h"

#include "../utils/string_utils.h"
#include "../utils/logger.h"

#include <windows.h>
#include <shlobj.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

#pragma comment(lib, "shell32.lib")

namespace {

std::string trimAscii(const std::string& s) {
    return StringUtils::trim(s);
}

std::string toUpperAscii(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return s;
}

}

ConfigManager& ConfigManager::instance() {
    static ConfigManager s_instance;
    return s_instance;
}

ConfigManager::ConfigManager() {
    m_iniPath = computeDefaultIniPath();
}

ConfigManager::~ConfigManager() = default;

std::wstring ConfigManager::computeDefaultIniPath() const {
    PWSTR roaming = nullptr;
    if (FAILED(::SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &roaming))) {
        return L"config.ini"; 
    }
    std::wstring dir = roaming;
    ::CoTaskMemFree(roaming);
    dir += L"\\TCGDrivePlugin";
    ::CreateDirectoryW(dir.c_str(), nullptr); 
    return dir + L"\\config.ini";
}

bool ConfigManager::getRaw(const std::string& key, std::string& out) const {
    auto it = m_values.find(key);
    if (it == m_values.end()) return false;
    out = it->second;
    return true;
}

void ConfigManager::setRaw(const std::string& key, const std::string& value) {
    m_values[key] = value;
}

template <>
std::string ConfigManager::get<std::string>(const std::string& key, std::string defaultValue) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::string raw;
    return getRaw(key, raw) ? raw : defaultValue;
}

template <>
std::wstring ConfigManager::get<std::wstring>(const std::string& key, std::wstring defaultValue) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::string raw;
    if (!getRaw(key, raw)) return defaultValue;
    return StringUtils::fromUtf8(raw);
}

template <>
int ConfigManager::get<int>(const std::string& key, int defaultValue) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::string raw;
    if (!getRaw(key, raw)) return defaultValue;
    try { return std::stoi(trimAscii(raw)); }
    catch (...) { return defaultValue; }
}

template <>
bool ConfigManager::get<bool>(const std::string& key, bool defaultValue) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::string raw;
    if (!getRaw(key, raw)) return defaultValue;
    const std::string v = toUpperAscii(trimAscii(raw));
    if (v == "1" || v == "TRUE" || v == "YES" || v == "ON")  return true;
    if (v == "0" || v == "FALSE"|| v == "NO"  || v == "OFF") return false;
    return defaultValue;
}


template <>
void ConfigManager::set<std::string>(const std::string& key, std::string value) {
    std::lock_guard<std::mutex> lock(m_mutex);
    setRaw(key, value);
}

template <>
void ConfigManager::set<std::wstring>(const std::string& key, std::wstring value) {
    std::lock_guard<std::mutex> lock(m_mutex);
    setRaw(key, StringUtils::toUtf8(value));
}

template <>
void ConfigManager::set<int>(const std::string& key, int value) {
    std::lock_guard<std::mutex> lock(m_mutex);
    setRaw(key, std::to_string(value));
}

template <>
void ConfigManager::set<bool>(const std::string& key, bool value) {
    std::lock_guard<std::mutex> lock(m_mutex);
    setRaw(key, value ? "true" : "false");
}


void ConfigManager::load() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_values.clear();

    std::ifstream in(m_iniPath, std::ios::binary);
    if (in) {   
        std::string line;
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            const std::string t = trimAscii(line);
            if (t.empty() || t[0] == ';' || t[0] == '#' || t[0] == '[') continue;
            const auto eq = t.find('=');
            if (eq == std::string::npos) continue;
            const std::string key = trimAscii(t.substr(0, eq));
            const std::string val = trimAscii(t.substr(eq + 1));
            if (!key.empty()) m_values[key] = val;
        }
    }

    if (m_values["client_id"].empty())     m_values["client_id"]     = DEFAULT_CLIENT_ID;
    if (m_values["client_secret"].empty()) m_values["client_secret"] = DEFAULT_CLIENT_SECRET;
}

void ConfigManager::save() {
    std::lock_guard<std::mutex> lock(m_mutex);

    const std::wstring tmp = m_iniPath + L".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) {
            Logger::instance().error("ConfigManager: cannot open temp file for write");
            return;
        }
        out << "[General]\r\n";
        for (const auto& kv : m_values) {
            if (kv.first == "client_id"     && kv.second == DEFAULT_CLIENT_ID)     continue;
            if (kv.first == "client_secret" && kv.second == DEFAULT_CLIENT_SECRET) continue;
            out << kv.first << '=' << kv.second << "\r\n";
        }
    }
    if (!::MoveFileExW(tmp.c_str(), m_iniPath.c_str(),
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        Logger::instance().error("ConfigManager: MoveFileExW failed: "
                                 + std::to_string(::GetLastError()));
        ::DeleteFileW(tmp.c_str());
    }
}

void ConfigManager::setIniPath(const std::wstring& path) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_iniPath = path.empty() ? computeDefaultIniPath() : path;
}

const std::wstring& ConfigManager::iniPath() const {
    return m_iniPath;
}

void ConfigManager::adoptIniPath(const std::wstring& tcIniPath) {
    if (tcIniPath.empty()) return;

    std::wstring current;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        current = m_iniPath;
    }
    const bool tcFileExists =
        (::GetFileAttributesW(tcIniPath.c_str()) != INVALID_FILE_ATTRIBUTES);
    const bool currentFileExists =
        (::GetFileAttributesW(current.c_str()) != INVALID_FILE_ATTRIBUTES);

    if (tcFileExists) {
        setIniPath(tcIniPath);
        load();
        Logger::instance().info("ConfigManager: adopted Total Commander INI path (loaded)");
    } else if (!currentFileExists) {
        setIniPath(tcIniPath);
        Logger::instance().info("ConfigManager: adopted Total Commander INI path (new config)");
    }
}


std::wstring ConfigManager::defaultAccount() const {
    return get<std::wstring>("default_account", L"");
}
void ConfigManager::setDefaultAccount(const std::wstring& email) {
    set<std::wstring>("default_account", email);
}

int  ConfigManager::cacheTtlSeconds() const {
    return get<int>("cache_ttl_seconds", 60);
}
void ConfigManager::setCacheTtlSeconds(int seconds) {
    set<int>("cache_ttl_seconds", seconds);
}

std::string ConfigManager::logLevelName() const {
    std::string v = toUpperAscii(trimAscii(get<std::string>("log_level", "INFO")));
    if (v == "DEBUG" || v == "INFO" || v == "WARNING" || v == "ERROR") return v;
    return "INFO";
}
void ConfigManager::setLogLevelName(const std::string& name) {
    const std::string v = toUpperAscii(trimAscii(name));
    set<std::string>("log_level",
                     (v == "DEBUG" || v == "INFO" || v == "WARNING" || v == "ERROR")
                         ? v : std::string("INFO"));
}

int ConfigManager::logLevel() const {
    const std::string v = logLevelName();
    if (v == "DEBUG")   return 0;
    if (v == "ERROR")   return 2;
    return 1;
}
void ConfigManager::setLogLevel(int comboIndex) {
    switch (comboIndex) {
        case 0:  setLogLevelName("DEBUG"); break;
        case 2:  setLogLevelName("ERROR"); break;
        default: setLogLevelName("INFO");  break;
    }
}

std::wstring ConfigManager::language() const {
    const std::wstring v = get<std::wstring>("language", L"uk");
    return (v == L"en") ? L"en" : L"uk";
}
void ConfigManager::setLanguage(const std::wstring& langCode) {
    set<std::wstring>("language", (langCode == L"en") ? L"en" : L"uk");
}

std::wstring ConfigManager::clientId() const {
    return get<std::wstring>("client_id", L"");
}
std::wstring ConfigManager::clientSecret() const {
    return get<std::wstring>("client_secret", L"");
}

bool ConfigManager::exportDocsAsDocx() const {
    return get<bool>("export_docs_as_docx", true);
}
void ConfigManager::setExportDocsAsDocx(bool enabled) {
    set<bool>("export_docs_as_docx", enabled);
}
bool ConfigManager::exportSheetsAsXlsx() const {
    return get<bool>("export_sheets_as_xlsx", true);
}
void ConfigManager::setExportSheetsAsXlsx(bool enabled) {
    set<bool>("export_sheets_as_xlsx", enabled);
}
