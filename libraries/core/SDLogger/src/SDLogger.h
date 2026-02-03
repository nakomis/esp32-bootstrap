#pragma once

#include <Arduino.h>
#include <SD_MMC.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/queue.h>
#include <freertos/task.h>

enum LogLevel {
    LOG_TRACE = 0,
    LOG_DEBUG = 1,
    LOG_INFO = 2,
    LOG_WARN = 3,
    LOG_ERROR = 4,
    LOG_CRITICAL = 5
};

struct LogEntry {
    LogLevel level;
    uint32_t timestamp;
    char message[256];
    bool immediate;
};

class SDLogger {
public:
    static SDLogger& getInstance();

    bool init(const char* logDir = "/logs");

    void trace(const char* message);
    void debug(const char* message);
    void info(const char* message);
    void warn(const char* message);
    void error(const char* message);
    void critical(const char* message);

    void tracef(const char* format, ...);
    void debugf(const char* format, ...);
    void infof(const char* format, ...);
    void warnf(const char* format, ...);
    void errorf(const char* format, ...);
    void criticalf(const char* format, ...);

    void log(LogLevel level, const char* message);
    void logf(LogLevel level, const char* format, ...);

    void flush();
    void rotateLogs();
    bool isInitialized() const { return _initialized; }

    void shutdown();
    uint32_t getDroppedCount() const { return _droppedCount; }

    void setLogLevel(LogLevel minLevel) { _minLogLevel = minLevel; }
    void setMaxFileSize(size_t maxSize) { _maxFileSize = maxSize; }
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
    size_t _maxFileSize = 1024 * 1024;
    bool _fileLoggingEnabled = true;
    uint32_t _bootCounter = 0;

    SemaphoreHandle_t _mutex = nullptr;
    QueueHandle_t _logQueue = nullptr;
    TaskHandle_t _writerTask = nullptr;
    volatile bool _shutdownRequested = false;
    volatile uint32_t _droppedCount = 0;

    static const int QUEUE_SIZE = 32;
    static const int WRITER_TASK_STACK_SIZE = 4096;
    static const int WRITER_TASK_PRIORITY = 1;

    bool enqueueLogEntry(LogLevel level, const char* message, bool immediate);
    void processLogEntry(const LogEntry& entry);
    static void writerTaskFunction(void* parameter);

    void writeToFile(const char* message);
    String formatLogEntry(LogLevel level, const char* message);
    String getLogLevelString(LogLevel level);
    String getCurrentTimestamp();
    String generateLogFileName();
    bool createLogDirectory();

    bool safeFileOperation(std::function<bool()> operation);
};

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
