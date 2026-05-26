#pragma once

#include <mutex>
#include <sstream>
#include <string>

enum class LogLevel { Debug = 0, Info = 1, Warning = 2, Error = 3 };

class Logger {
public:
    static Logger& instance();

    void     setLevel(LogLevel level);
    LogLevel level() const;

    void log(LogLevel level, const std::string& message);

    void debug(const std::string& msg) { log(LogLevel::Debug,   msg); }
    void info (const std::string& msg) { log(LogLevel::Info,    msg); }
    void warn (const std::string& msg) { log(LogLevel::Warning, msg); }
    void error(const std::string& msg) { log(LogLevel::Error,   msg); }

private:
    Logger();
    ~Logger();
    Logger(const Logger&)            = delete;
    Logger& operator=(const Logger&) = delete;

    std::wstring        activePath() const;          // the %APPDATA% log file
    void                rotateIfNeeded(const std::wstring& path);
    static const char*  levelName(LogLevel l);

    mutable std::mutex m_mutex;
    LogLevel           m_level = LogLevel::Info;

    static constexpr long long kMaxBytes      = 5LL * 1024 * 1024; // 5 MiB
    static constexpr int       kKeptFiles     = 3;                 // current + 2 rotated
};

namespace logdetail {

class Line {
public:
    explicit Line(LogLevel lvl) : m_level(lvl) {}
    ~Line() { Logger::instance().log(m_level, m_oss.str()); }
    std::ostream& stream() { return m_oss; }
private:
    LogLevel           m_level;
    std::ostringstream m_oss;
};

}

#define LOG_DEBUG(msg)   (void)(::logdetail::Line(::LogLevel::Debug).stream()   << msg)
#define LOG_INFO(msg)    (void)(::logdetail::Line(::LogLevel::Info).stream()    << msg)
#define LOG_WARNING(msg) (void)(::logdetail::Line(::LogLevel::Warning).stream() << msg)
#define LOG_ERROR(msg)   (void)(::logdetail::Line(::LogLevel::Error).stream()   << msg)
