// Logging utilities (spdlog wrapper)
#include "logger.h"

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <iostream>

namespace romexplorer {

static std::shared_ptr<spdlog::logger> g_logger;

Logger::Logger() = default;
Logger::~Logger() = default;

Logger& Logger::instance()
{
    static Logger s_instance;
    return s_instance;
}

void Logger::initialize(LogLevel level)
{
    if (!g_logger) {
        // Create console sink with color
        auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        
        // Create logger with the sink
        g_logger = std::make_shared<spdlog::logger>("romexplorer", console_sink);
        
        // Set pattern: [LEVEL] message
        g_logger->set_pattern("[%^%l%$] %v");
        
        // Register as default logger
        spdlog::register_logger(g_logger);
    }
    
    // Set level
    Logger::instance().set_level(level);
}

void Logger::set_level(LogLevel level)
{
    if (!g_logger) {
        initialize(level);
        return;
    }
    
    switch (level) {
        case LogLevel::TRACE:    g_logger->set_level(spdlog::level::trace); break;
        case LogLevel::DEBUG:    g_logger->set_level(spdlog::level::debug); break;
        case LogLevel::INFO:     g_logger->set_level(spdlog::level::info); break;
        case LogLevel::WARN:     g_logger->set_level(spdlog::level::warn); break;
        case LogLevel::ERR:      g_logger->set_level(spdlog::level::err); break;
        case LogLevel::CRITICAL: g_logger->set_level(spdlog::level::critical); break;
        case LogLevel::OFF:      g_logger->set_level(spdlog::level::off); break;
    }
}

void Logger::trace(const std::string& message)
{
    if (!g_logger) initialize();
    g_logger->trace(message);
}

void Logger::debug(const std::string& message)
{
    if (!g_logger) initialize();
    g_logger->debug(message);
}

void Logger::info(const std::string& message)
{
    if (!g_logger) initialize();
    g_logger->info(message);
}

void Logger::warn(const std::string& message)
{
    if (!g_logger) initialize();
    g_logger->warn(message);
}

void Logger::error(const std::string& message)
{
    if (!g_logger) initialize();
    g_logger->error(message);
}

void Logger::critical(const std::string& message)
{
    if (!g_logger) initialize();
    g_logger->critical(message);
}

} // namespace romexplorer
