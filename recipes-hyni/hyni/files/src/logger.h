#ifndef LOGGING_H
#define LOGGING_H

#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <memory>
#include <fstream>

class logger {
public:
    // Log levels
    enum class Level {
        DEBUG,
        INFO,
        WARNING,
        ERROR
    };

    // Delete copy/move operations to enforce singleton
    logger(const logger&) = delete;
    logger& operator=(const logger&) = delete;
    logger(logger&&) = delete;
    logger& operator=(logger&&) = delete;

    // Singleton access
    static logger& instance();

    // Initialization
    void init(bool enable_file_logging = true, bool enable_console_logging = true);

    // Check if logging is enabled
    bool is_enabled() const;

    // Core logging function
    void log(Level level, const std::string& message,
             const std::string& file = "", int line = -1);

    // Log section with title and messages
    void log_section(const std::string& title,
                     const std::vector<std::string>& messages,
                     Level level = Level::INFO);

    // Set the minimum log level to output
    void set_min_level(Level level);

    // Get current log file name
    std::string get_log_file_name() const;

    // Flush the log file
    void flush();

    // Shutdown the logging system
    void shutdown();

    // Utility functions
    static std::string truncate_text(const std::string& text, size_t max_length = 100);
    static std::string get_json_keys(const nlohmann::json& j);

private:
    logger() = default; // Private constructor
    ~logger(); // Private destructor

    // Internal state
    struct loggerState {
        bool file_logging_enabled = false;
        bool console_logging_enabled = true;
        Level min_level = Level::DEBUG;
        std::ofstream log_file;
        std::string log_file_name;
    };

    std::unique_ptr<loggerState> m_state;

    // Helper methods
    std::string generate_log_filename();
    std::string level_to_string(Level level) const;
    std::string current_time() const;
};

// Convenience macros for easier logging
#define LOG_DEBUG(msg) logger::instance().log(logger::Level::DEBUG, msg, __FILE__, __LINE__)
#define LOG_INFO(msg) logger::instance().log(logger::Level::INFO, msg, __FILE__, __LINE__)
#define LOG_WARNING(msg) logger::instance().log(logger::Level::WARNING, msg, __FILE__, __LINE__)
#define LOG_ERROR(msg) logger::instance().log(logger::Level::ERROR, msg, __FILE__, __LINE__)

#endif // LOGGING_H
