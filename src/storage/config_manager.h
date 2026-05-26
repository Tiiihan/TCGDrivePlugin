#pragma once

#include <map>
#include <mutex>
#include <string>

class ConfigManager {
public:
    static constexpr const char* DEFAULT_CLIENT_ID     = "";
    static constexpr const char* DEFAULT_CLIENT_SECRET = "";

    static ConfigManager& instance();

    template <typename T> T    get(const std::string& key, T defaultValue) const;
    template <typename T> void set(const std::string& key, T value);

    void load();
    void save();

    void                setIniPath(const std::wstring& path);
    const std::wstring& iniPath() const;

    void adoptIniPath(const std::wstring& tcIniPath);


    std::wstring defaultAccount() const;
    void         setDefaultAccount(const std::wstring& email);

    int  cacheTtlSeconds() const;
    void setCacheTtlSeconds(int seconds);

    int          logLevel() const;
    void         setLogLevel(int comboIndex);
    std::string  logLevelName() const;             // "DEBUG".."ERROR"
    void         setLogLevelName(const std::string& name);

    std::wstring language() const;                 // "uk" | "en"
    void         setLanguage(const std::wstring& langCode);

    std::wstring clientId() const;
    std::wstring clientSecret() const;

    bool exportDocsAsDocx() const;
    void setExportDocsAsDocx(bool enabled);
    bool exportSheetsAsXlsx() const;
    void setExportSheetsAsXlsx(bool enabled);

private:
    ConfigManager();
    ~ConfigManager();
    ConfigManager(const ConfigManager&)            = delete;
    ConfigManager& operator=(const ConfigManager&) = delete;

    bool getRaw(const std::string& key, std::string& out) const;
    void setRaw(const std::string& key, const std::string& value);

    std::wstring computeDefaultIniPath() const;

    mutable std::mutex                 m_mutex;
    std::map<std::string, std::string> m_values;
    std::wstring                       m_iniPath;
};

template <> std::string  ConfigManager::get<std::string >(const std::string&, std::string ) const;
template <> std::wstring ConfigManager::get<std::wstring>(const std::string&, std::wstring) const;
template <> int          ConfigManager::get<int         >(const std::string&, int         ) const;
template <> bool         ConfigManager::get<bool        >(const std::string&, bool        ) const;

template <> void ConfigManager::set<std::string >(const std::string&, std::string );
template <> void ConfigManager::set<std::wstring>(const std::string&, std::wstring);
template <> void ConfigManager::set<int         >(const std::string&, int         );
template <> void ConfigManager::set<bool        >(const std::string&, bool        );
