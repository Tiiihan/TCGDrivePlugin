#include "logger.h"

#include <windows.h>
#include <shlobj.h>

#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>

#pragma comment(lib, "shell32.lib")

namespace {

std::wstring appDataDir() {
    PWSTR roaming = nullptr;
    if (FAILED(::SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &roaming))) {
        return L".";
    }
    std::wstring dir = roaming;
    ::CoTaskMemFree(roaming);
    dir += L"\\TCGDrivePlugin";
    ::CreateDirectoryW(dir.c_str(), nullptr);
    return dir;
}

long long fileSizeW(const std::wstring& path) {
    WIN32_FILE_ATTRIBUTE_DATA fad{};
    if (!::GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fad)) return -1;
    LARGE_INTEGER li;
    li.HighPart = static_cast<LONG>(fad.nFileSizeHigh);
    li.LowPart  = fad.nFileSizeLow;
    return li.QuadPart;
}

std::string timestampNow() {
    using namespace std::chrono;
    const auto now  = system_clock::now();
    const auto secs = system_clock::to_time_t(now);
    const auto ms   = duration_cast<milliseconds>(now.time_since_epoch()).count() % 1000;

    std::tm tm{};
    ::localtime_s(&tm, &secs);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d.%03lld",
                  tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                  tm.tm_hour, tm.tm_min, tm.tm_sec,
                  static_cast<long long>(ms));
    return buf;
}

} // namespace

Logger& Logger::instance() {
    static Logger s_instance;
    return s_instance;
}

Logger::Logger()  = default;
Logger::~Logger() = default;

void Logger::setLevel(LogLevel level) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_level = level;
}

LogLevel Logger::level() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_level;
}

const char* Logger::levelName(LogLevel l) {
    switch (l) {
        case LogLevel::Debug:   return "DEBUG";
        case LogLevel::Info:    return "INFO";
        case LogLevel::Warning: return "WARNING";
        case LogLevel::Error:   return "ERROR";
    }
    return "?";
}

std::wstring Logger::activePath() const {
    return appDataDir() + L"\\plugin.log";
}

void Logger::rotateIfNeeded(const std::wstring& path) {
    const long long sz = fileSizeW(path);
    if (sz < 0 || sz < kMaxBytes) return;

    const int lastIdx = kKeptFiles - 1;       
    const std::wstring oldest = path + L"." + std::to_wstring(lastIdx);
    ::DeleteFileW(oldest.c_str());

    for (int i = lastIdx - 1; i >= 1; --i) {
        const std::wstring from = path + L"." + std::to_wstring(i);
        const std::wstring to   = path + L"." + std::to_wstring(i + 1);
        ::MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING);
    }
    const std::wstring rotated1 = path + L".1";
    ::MoveFileExW(path.c_str(), rotated1.c_str(), MOVEFILE_REPLACE_EXISTING);
}

void Logger::log(LogLevel level, const std::string& message) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (static_cast<int>(level) < static_cast<int>(m_level)) return;

    const std::wstring path = activePath();
    rotateIfNeeded(path);

    std::ofstream out(path, std::ios::app | std::ios::binary);
    if (!out) return;

    out << timestampNow() << " [" << levelName(level) << "] " << message << "\r\n";
}
