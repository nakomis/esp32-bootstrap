#ifndef CATCAM_SDLOGGER_H
#define CATCAM_SDLOGGER_H

#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <vector>

enum LogLevel {
    LOG_TRACE = 0,
    LOG_DEBUG = 1,
    LOG_INFO = 2,
    LOG_WARN = 3,
    LOG_ERROR = 4,
    LOG_CRITICAL = 5
};

// Log entry structure for async queue
struct LogEntry {
    LogLevel level;
    uint32_t timestamp;      // millis() when enqueued
    char message[256];       // Log message content
    bool immediate;          // If true, should be written immediately (critical logs)
};

class SDLogger {
public:
    static SDLogger& getInstance();
    
    // Initialize SD card and logger
    bool init(const char* logDir = "/logs");
    
    // Thread-safe logging methods
    void trace(const char* message);
    void debug(const char* message);
    void info(const char* message);
    void warn(const char* message);
    void error(const char* message);
    void critical(const char* message);
    
    // Formatted logging
    void tracef(const char* format, ...);
    void debugf(const char* format, ...);
    void infof(const char* format, ...);
    void warnf(const char* format, ...);
    void errorf(const char* format, ...);
    void criticalf(const char* format, ...);
    
    // Generic log method
    void log(LogLevel level, const char* message);
    void logf(LogLevel level, const char* format, ...);
    
    // Deterrent-specific logging
    void logDeterrentActivation(const char* catName, float confidence, const float* allProbs);
    void logDeterrentRejection(const char* catName, float confidence, const char* reason);
    void logDetection(const char* catName, float confidence, int pictureNumber);
    
    // File management
    void flush();
    void rotateLogs();
    bool isInitialized() const { return _initialized; }

    // Async queue management
    void shutdown();
    uint32_t getDroppedCount() const { return _droppedCount; }
    uint32_t getTotalEnqueued() const { return _totalEnqueued; }
    uint32_t getTotalWritten() const { return _totalWritten; }
    uint32_t getQueueDepth() const;

    // Log retrieval
    String getRecentLogEntries(int maxLines = 50);
    void processRecentLogEntries(int maxLines, std::function<void(const String&)> processor);

    // Previous log files
    std::vector<String> listLogFiles();
    void processLogFile(const String& filename, int maxLines, std::function<void(const String&)> processor);

    // Configuration
    void setLogLevel(LogLevel minLevel) { _minLogLevel = minLevel; }
    void setMaxFileSize(size_t maxSize) { _maxFileSize = maxSize; }
    void setMaxFiles(int maxFiles) { _maxFiles = maxFiles; }
    void setFileLoggingEnabled(bool enabled) { _fileLoggingEnabled = enabled; }

private:
    SDLogger() = default;
    ~SDLogger() = default;
    SDLogger(const SDLogger&) = delete;
    SDLogger& operator=(const SDLogger&) = delete;
    
    bool _initialized = false;
    String _logDir = "/logs";
    String _currentLogFile;
    LogLevel _minLogLevel = LOG_INFO;
    size_t _maxFileSize = 1024 * 1024; // 1MB default
    int _maxFiles = 10;
    bool _fileLoggingEnabled = true;
    uint32_t _bootCounter = 0;

    // Thread safety
    SemaphoreHandle_t _mutex = nullptr;

    // Async queue members
    QueueHandle_t _logQueue = nullptr;
    TaskHandle_t _writerTask = nullptr;
    SemaphoreHandle_t _flushSemaphore = nullptr;
    volatile bool _shutdownRequested = false;
    volatile uint32_t _droppedCount = 0;
    volatile uint32_t _totalEnqueued = 0;
    volatile uint32_t _totalWritten = 0;
    uint32_t _lastDropWarning = 0;

    // Async queue constants
    static const int QUEUE_SIZE = 64;
    static const int BATCH_SIZE = 8;
    static const int WRITER_TASK_STACK_SIZE = 4096;
    static const int WRITER_TASK_PRIORITY = 1;

    // Async queue methods
    bool enqueueLogEntry(LogLevel level, const char* message, bool immediate);
    void processLogEntry(const LogEntry& entry);
    static void writerTaskFunction(void* parameter);

    // Internal methods
    void writeToFile(const char* message);
    String formatLogEntry(LogLevel level, const char* message);
    String getLogLevelString(LogLevel level);
    String getCurrentTimestamp();
    String generateLogFileName();
    bool createLogDirectory();
    void cleanupOldLogs();
    
    // Thread-safe wrapper for file operations
    bool safeFileOperation(std::function<bool()> operation);
};

// Convenience macros for easier logging
#define LOG_T(msg) SDLogger::getInstance().trace(msg)
#define LOG_D(msg) SDLogger::getInstance().debug(msg)
#define LOG_I(msg) SDLogger::getInstance().info(msg)
#define LOG_W(msg) SDLogger::getInstance().warn(msg)
#define LOG_E(msg) SDLogger::getInstance().error(msg)
#define LOG_C(msg) SDLogger::getInstance().critical(msg)

#define LOG_TF(fmt, ...) SDLogger::getInstance().tracef(fmt, ##__VA_ARGS__)
#define LOG_DF(fmt, ...) SDLogger::getInstance().debugf(fmt, ##__VA_ARGS__)
#define LOG_IF(fmt, ...) SDLogger::getInstance().infof(fmt, ##__VA_ARGS__)
#define LOG_WF(fmt, ...) SDLogger::getInstance().warnf(fmt, ##__VA_ARGS__)
#define LOG_EF(fmt, ...) SDLogger::getInstance().errorf(fmt, ##__VA_ARGS__)
#define LOG_CF(fmt, ...) SDLogger::getInstance().criticalf(fmt, ##__VA_ARGS__)

#endif
