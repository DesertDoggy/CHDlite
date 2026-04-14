#pragma once

#include <string>
#include <memory>

namespace romexplorer {

enum class LogLevel {
    TRACE,
    DEBUG,
    INFO,
    WARN,
    ERR,
    CRITICAL,
    OFF
};

class Logger {
public:
    static Logger& instance();
    
    // Initialize logger with level
    static void initialize(LogLevel level = LogLevel::INFO);
    
    // Log functions
    void trace(const std::string& message);
    void debug(const std::string& message);
    void info(const std::string& message);
    void warn(const std::string& message);
    void error(const std::string& message);
    void critical(const std::string& message);
    
    // Set log level
    void set_level(LogLevel level);
    
private:
    Logger();
    ~Logger();
};

} // namespace romexplorer

// Convenience macros for logging
#define LOG_TRACE(msg) romexplorer::Logger::instance().trace(msg)
#define LOG_DEBUG(msg) romexplorer::Logger::instance().debug(msg)
#define LOG_INFO(msg)  romexplorer::Logger::instance().info(msg)
#define LOG_WARN(msg)  romexplorer::Logger::instance().warn(msg)
#define LOG_ERROR(msg) romexplorer::Logger::instance().error(msg)
#define LOG_CRIT(msg)  romexplorer::Logger::instance().critical(msg)

// String building helpers for logging
#include <sstream>

#define LOG_DEBUG_STREAM(expr) \
    do { \
        std::ostringstream __oss__; \
        __oss__ << expr; \
        LOG_DEBUG(__oss__.str()); \
    } while(0)

#define LOG_INFO_STREAM(expr) \
    do { \
        std::ostringstream __oss__; \
        __oss__ << expr; \
        LOG_INFO(__oss__.str()); \
    } while(0)
