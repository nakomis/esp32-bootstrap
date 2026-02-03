#include "SDLogger.h"
#include <time.h>
#include <sys/time.h>
#include <functional>
#include <FS.h>
#include <Preferences.h>

// SD card pins for ESP32-S3 (SDMMC 1-bit mode)
#define SD_CLK_PIN 39
#define SD_CMD_PIN 38
#define SD_D0_PIN  40

SDLogger& SDLogger::getInstance() {
    static SDLogger instance;
    return instance;
}

bool SDLogger::init(const char* logDir) {
    if (_initialized) {
        Serial.println("SDLogger: Already initialized");
        return true;
    }

    // Set custom SD card pins for ESP32-S3
    Serial.println("SDLogger: Initializing SD card...");

    // Enable internal pull-ups on SD card pins (needed when camera not attached)
    pinMode(SD_CLK_PIN, INPUT_PULLUP);
    pinMode(SD_CMD_PIN, INPUT_PULLUP);
    pinMode(SD_D0_PIN, INPUT_PULLUP);
    delay(10);  // Let pull-ups stabilize

    SD_MMC.setPins(SD_CLK_PIN, SD_CMD_PIN, SD_D0_PIN);

    if (!SD_MMC.begin("/sdcard", true)) {  // 1-bit mode
        Serial.println("SDLogger: SD_MMC.begin() FAILED - check SD card and pull-up resistors");
        return false;
    }
    Serial.println("SDLogger: SD card initialized");

    _logDir = String(logDir);

    // Read and increment boot counter from NVS
    Preferences prefs;
    prefs.begin("sdlogger", false);
    _bootCounter = prefs.getUInt("boot_count", 0);
    _bootCounter++;
    prefs.putUInt("boot_count", _bootCounter);
    prefs.end();

    // Create mutex for thread safety
    _mutex = xSemaphoreCreateMutex();
    if (_mutex == nullptr) {
        Serial.println("SDLogger: Failed to create mutex");
        return false;
    }

    // Create log directory
    if (!createLogDirectory()) {
        Serial.println("SDLogger: Failed to create log directory");
        return false;
    }

    // Generate initial log file name
    _currentLogFile = generateLogFileName();

    // Create log queue for async writes
    _logQueue = xQueueCreate(QUEUE_SIZE, sizeof(LogEntry));
    if (_logQueue == nullptr) {
        Serial.println("SDLogger: Failed to create log queue");
        return false;
    }

    // Create writer task
    _shutdownRequested = false;
    BaseType_t taskResult = xTaskCreate(
        writerTaskFunction,
        "LogWriter",
        WRITER_TASK_STACK_SIZE,
        this,
        WRITER_TASK_PRIORITY,
        &_writerTask
    );

    if (taskResult != pdPASS) {
        Serial.println("SDLogger: Failed to create writer task");
        vQueueDelete(_logQueue);
        _logQueue = nullptr;
        return false;
    }

    _initialized = true;

    info("SDLogger initialized (async mode)");
    infof("Boot count: %u", _bootCounter);
    infof("Log file: %s", _currentLogFile.c_str());

    return true;
}

void SDLogger::trace(const char* message) { log(LOG_TRACE, message); }
void SDLogger::debug(const char* message) { log(LOG_DEBUG, message); }
void SDLogger::info(const char* message) { log(LOG_INFO, message); }
void SDLogger::warn(const char* message) { log(LOG_WARN, message); }
void SDLogger::error(const char* message) { log(LOG_ERROR, message); }
void SDLogger::critical(const char* message) { log(LOG_CRITICAL, message); }

void SDLogger::tracef(const char* format, ...) {
    va_list args;
    va_start(args, format);
    char buffer[512];
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    trace(buffer);
}

void SDLogger::debugf(const char* format, ...) {
    va_list args;
    va_start(args, format);
    char buffer[512];
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    debug(buffer);
}

void SDLogger::infof(const char* format, ...) {
    va_list args;
    va_start(args, format);
    char buffer[512];
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    info(buffer);
}

void SDLogger::warnf(const char* format, ...) {
    va_list args;
    va_start(args, format);
    char buffer[512];
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    warn(buffer);
}

void SDLogger::errorf(const char* format, ...) {
    va_list args;
    va_start(args, format);
    char buffer[512];
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    error(buffer);
}

void SDLogger::criticalf(const char* format, ...) {
    va_list args;
    va_start(args, format);
    char buffer[512];
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    critical(buffer);
}

void SDLogger::log(LogLevel level, const char* message) {
    if (level < _minLogLevel) {
        return;
    }

    String logEntry = formatLogEntry(level, message);

    // Always print to Serial
    Serial.print(logEntry);

    // Enqueue for async write if available
    if (_initialized && _logQueue != nullptr) {
        bool immediate = (level >= LOG_CRITICAL);
        enqueueLogEntry(level, message, immediate);
    } else if (_initialized) {
        writeToFile(logEntry.c_str());
    }
}

void SDLogger::logf(LogLevel level, const char* format, ...) {
    va_list args;
    va_start(args, format);
    char buffer[512];
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    log(level, buffer);
}

void SDLogger::flush() {
    if (!_initialized || _logQueue == nullptr) return;

    const int maxWaitMs = 5000;
    const int pollIntervalMs = 50;
    int waited = 0;

    while (uxQueueMessagesWaiting(_logQueue) > 0 && waited < maxWaitMs) {
        vTaskDelay(pdMS_TO_TICKS(pollIntervalMs));
        waited += pollIntervalMs;
    }

    if (waited >= maxWaitMs) {
        Serial.println("SDLogger: flush() timed out waiting for queue to drain");
    }

    // CRITICAL: Ensure any pending file operations are complete
    // This acquires the mutex, ensuring the writer task has finished
    safeFileOperation([this]() {
        return true;
    });
}

void SDLogger::rotateLogs() {
    if (!_initialized) return;

    safeFileOperation([this]() {
        File currentFile = SD_MMC.open(_currentLogFile, FILE_READ);
        if (currentFile && currentFile.size() > _maxFileSize) {
            currentFile.close();
            _currentLogFile = generateLogFileName();
            infof("Log rotated to: %s", _currentLogFile.c_str());
        }
        if (currentFile) currentFile.close();
        return true;
    });
}

void SDLogger::writeToFile(const char* message) {
    if (!_initialized || !_fileLoggingEnabled) return;

    safeFileOperation([this, message]() {
        File file = SD_MMC.open(_currentLogFile.c_str(), FILE_APPEND);
        if (file) {
            file.print(message);
            file.close();

            // Check if rotation is needed
            File sizeCheck = SD_MMC.open(_currentLogFile.c_str(), FILE_READ);
            if (sizeCheck && sizeCheck.size() > _maxFileSize) {
                sizeCheck.close();
                rotateLogs();
            } else if (sizeCheck) {
                sizeCheck.close();
            }
            return true;
        }
        return false;
    });
}

String SDLogger::formatLogEntry(LogLevel level, const char* message) {
    String timestamp = getCurrentTimestamp();
    String levelStr = getLogLevelString(level);
    return timestamp + " [" + levelStr + "] " + String(message) + "\n";
}

String SDLogger::getLogLevelString(LogLevel level) {
    switch (level) {
        case LOG_TRACE: return "TRACE";
        case LOG_DEBUG: return "DEBUG";
        case LOG_INFO: return "INFO";
        case LOG_WARN: return "WARN";
        case LOG_ERROR: return "ERROR";
        case LOG_CRITICAL: return "CRIT";
        default: return "UNKNOWN";
    }
}

String SDLogger::getCurrentTimestamp() {
    struct timeval tv;
    gettimeofday(&tv, nullptr);

    struct tm* timeinfo = localtime(&tv.tv_sec);
    char buffer[32];
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", timeinfo);

    return String(buffer) + "." + String(tv.tv_usec / 1000, DEC);
}

String SDLogger::generateLogFileName() {
    char buffer[64];
    snprintf(buffer, sizeof(buffer), "%04u_kappa_%lu.log", _bootCounter, millis());
    return _logDir + "/" + String(buffer);
}

bool SDLogger::createLogDirectory() {
    if (!SD_MMC.exists(_logDir)) {
        return SD_MMC.mkdir(_logDir);
    }
    return true;
}

bool SDLogger::safeFileOperation(std::function<bool()> operation) {
    if (!_initialized || _mutex == nullptr) {
        return false;
    }

    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        bool result = operation();
        xSemaphoreGive(_mutex);
        return result;
    }

    return false;
}

bool SDLogger::enqueueLogEntry(LogLevel level, const char* message, bool immediate) {
    if (_logQueue == nullptr) {
        return false;
    }

    LogEntry entry;
    entry.level = level;
    entry.timestamp = millis();
    entry.immediate = immediate;
    strncpy(entry.message, message, sizeof(entry.message) - 1);
    entry.message[sizeof(entry.message) - 1] = '\0';

    BaseType_t result = xQueueSend(_logQueue, &entry, pdMS_TO_TICKS(10));

    if (result != pdTRUE) {
        _droppedCount++;
    }

    return result == pdTRUE;
}

void SDLogger::processLogEntry(const LogEntry& entry) {
    String logEntry = formatLogEntry(entry.level, entry.message);
    writeToFile(logEntry.c_str());
}

void SDLogger::writerTaskFunction(void* parameter) {
    SDLogger* logger = static_cast<SDLogger*>(parameter);
    LogEntry entry;

    while (!logger->_shutdownRequested) {
        if (xQueueReceive(logger->_logQueue, &entry, pdMS_TO_TICKS(100)) == pdTRUE) {
            logger->processLogEntry(entry);

            // Batch additional queued entries
            int batchCount = 1;
            while (batchCount < 8 && xQueueReceive(logger->_logQueue, &entry, 0) == pdTRUE) {
                logger->processLogEntry(entry);
                batchCount++;
            }

            taskYIELD();
        }
    }

    // Drain remaining queue entries
    while (xQueueReceive(logger->_logQueue, &entry, 0) == pdTRUE) {
        logger->processLogEntry(entry);
    }

    vTaskDelete(NULL);
}

void SDLogger::shutdown() {
    if (_writerTask == nullptr) {
        return;
    }

    info("SDLogger: Shutting down...");
    _shutdownRequested = true;

    // Wait for task to finish
    vTaskDelay(pdMS_TO_TICKS(1000));

    if (_logQueue != nullptr) {
        vQueueDelete(_logQueue);
        _logQueue = nullptr;
    }

    _writerTask = nullptr;
}
