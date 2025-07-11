#include "logger.h"
#include <filesystem>
#include <iostream>
#include <iomanip>
#include <chrono>
#include <ctime>
#include <fstream>
#include <sstream>

logger& logger::instance() {
    static logger instance;
    return instance;
}

logger::~logger() {
    shutdown();
}

void logger::init(bool enable_file_logging, bool enable_console_logging) {
    if (m_state) {
        shutdown(); // Clean up if already initialized
    }

    m_state = std::make_unique<loggerState>();
    m_state->file_logging_enabled = enable_file_logging;
    m_state->console_logging_enabled = enable_console_logging;

    if (enable_file_logging) {
        m_state->log_file_name = generate_log_filename();
        m_state->log_file.open(m_state->log_file_name, std::ios::out | std::ios::app);

        if (!m_state->log_file.is_open()) {
            std::cerr << "Failed to open log file: " << m_state->log_file_name << std::endl;
            m_state->file_logging_enabled = false;
        } else {
            // Write initial header
            m_state->log_file << "=== Logging started ===" << std::endl;
            m_state->log_file << "Log file: " << m_state->log_file_name << std::endl;
            m_state->log_file << "====" << std::endl << std::endl;
        }
    }
}

bool logger::is_enabled() const {
    return m_state && (m_state->file_logging_enabled || m_state->console_logging_enabled);
}

std::string logger::generate_log_filename() {
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);

    std::tm tm_buf;
    localtime_r(&in_time_t, &tm_buf);

    std::stringstream ss;
    ss << "hyni_log_"
       << std::put_time(&tm_buf, "%Y%m%d_%H%M%S")
       << ".log";
    return ss.str();
}

std::string logger::level_to_string(Level level) const {
    switch(level) {
    case Level::DEBUG:   return "DEBUG";
    case Level::INFO:    return "INFO";
    case Level::WARNING: return "WARNING";
    case Level::ERROR:   return "ERROR";
    default:             return "UNKNOWN";
    }
}

std::string logger::current_time() const {
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);

    std::tm tm_buf;
    localtime_r(&in_time_t, &tm_buf);

    std::stringstream ss;
    ss << std::put_time(&tm_buf, "%Y-%m-%d %X");
    return ss.str();
}

void logger::log(Level level, const std::string& message,
                 const std::string& file, int line) {
    if (!m_state || level < m_state->min_level) return;

    std::stringstream log_entry;
    log_entry << "[" << current_time() << "] "
              << "[" << level_to_string(level) << "] ";

    if (!file.empty() && line != -1) {
        log_entry << "[" << std::filesystem::path(file).filename().string()
        << ":" << line << "] ";
    }

    log_entry << message;

    const std::string final_message = log_entry.str();

    if (m_state->console_logging_enabled) {
        std::cerr << final_message << std::endl;
    }

    if (m_state->file_logging_enabled && m_state->log_file.is_open()) {
        m_state->log_file << final_message << std::endl;
    }
}

void logger::log_section(const std::string& title,
                         const std::vector<std::string>& messages,
                         Level level) {
    if (!m_state || level < m_state->min_level) return;

    log(level, "\n==== " + title + " ====");
    for (const auto& msg : messages) {
        log(level, msg);
    }
    log(level, "=====================================");
}

void logger::set_min_level(Level level) {
    if (m_state) {
        m_state->min_level = level;
    }
}

std::string logger::get_log_file_name() const {
    return m_state ? m_state->log_file_name : "";
}

void logger::flush() {
    if (m_state && m_state->file_logging_enabled && m_state->log_file.is_open()) {
        m_state->log_file.flush();
    }
}

void logger::shutdown() {
    if (m_state) {
        if (m_state->file_logging_enabled && m_state->log_file.is_open()) {
            m_state->log_file << std::endl << "=== Logging ended ===" << std::endl;
            m_state->log_file.close();
        }
        m_state.reset();
    }
}

std::string logger::truncate_text(const std::string& text, size_t max_length) {
    return text.length() > max_length ? text.substr(0, max_length) + "..." : text;
}

std::string logger::get_json_keys(const nlohmann::json& j) {
    std::string keys;
    for (auto it = j.begin(); it != j.end(); ++it) {
        if (!keys.empty()) keys += ", ";
        keys += it.key();
    }
    return keys.empty() ? "(none)" : keys;
}
