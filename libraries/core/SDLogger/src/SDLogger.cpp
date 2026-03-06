#include "SDLogger.h"
#include <time.h>
#include <sys/time.h>
#include <functional>
#include <algorithm>
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
    delay(10);  // Let pull-ups stabilise

    SD_MMC.setPins(SD_CLK_PIN, SD_CMD_PIN, SD_D0_PIN);

    if (!SD_MMC.begin("/sdcard", true)) {  // 1-bit mode
        Serial.println("SDLogger: SD_MMC.begin() FAILED - check SD card and pull-up resistors");
        return false;
    }
    Serial.println("SDLogger: SD card initialized");

    _logDir = String(logDir);

    // Read and increment boot counter from NVS
    Preferences prefs;
    prefs.begin("sdlogger", false);  // read-write
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

    // Create semaphore used to signal that the writer task has exited
    _writerExitSemaphore = xSemaphoreCreateBinary();
    if (_writerExitSemaphore == nullptr) {
        Serial.println("SDLogger: Failed to create writer exit semaphore");
        return false;
    }

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
    infof("Queue size: %d entries", QUEUE_SIZE);

    // Clean up old log files on boot
    if (_maxFiles > 0) {
        cleanupOldLogs();
    }

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

    // Always print to Serial for immediate feedback
    Serial.print(logEntry);

    // Enqueue for async write if queue is available
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
    if (!_initialized) return;

    // If async queue is available, wait for it to drain
    if (_logQueue != nullptr && _writerExitSemaphore != nullptr) {
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
    }

    // Ensure any pending file operations are complete
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
            cleanupOldLogs();
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

void SDLogger::cleanupOldLogs() {
    // Get sorted list of log files (oldest first due to naming convention)
    // Uses internal variant — cleanupOldLogs is called from within mutex-held context
    std::vector<String> logFiles = listLogFilesInternal();

    if ((int)logFiles.size() <= _maxFiles) {
        return;  // Nothing to clean up
    }

    int filesToDelete = logFiles.size() - _maxFiles;
    infof("Cleaning up old logs: %d files to delete (have %d, max %d)",
          filesToDelete, (int)logFiles.size(), _maxFiles);

    // Delete oldest files (they're at the start of the sorted list)
    for (int i = 0; i < filesToDelete; i++) {
        String filepath = _logDir + "/" + logFiles[i];
        if (SD_MMC.remove(filepath.c_str())) {
            infof("Deleted old log: %s", logFiles[i].c_str());
        } else {
            warnf("Failed to delete log: %s", logFiles[i].c_str());
        }
    }
}

String SDLogger::getRecentLogEntries(int maxLines) {
    if (!_initialized) {
        return "{\"error\":\"Logger not initialized\"}";
    }

    if (maxLines == 0) return String();
    if (maxLines < 0) maxLines = 200;  // treat negative as "last 200 lines"

    String result = "";
    int lineCount = 0;

    bool success = safeFileOperation([this, maxLines, &result, &lineCount]() {
        File file = SD_MMC.open(_currentLogFile.c_str(), FILE_READ);
        if (!file) {
            result = "{\"error\":\"Failed to open log file\"}";
            return false;
        }

        // Use circular buffer for last N lines
        String* lines = new String[maxLines];
        int currentLine = 0;
        int totalLines = 0;

        while (file.available()) {
            String line = file.readStringUntil('\n');
            if (line.length() > 0) {
                lines[currentLine % maxLines] = line;
                currentLine++;
                totalLines++;
            }
        }
        file.close();

        // Build JSON array with the last N lines
        result = "[";
        int startIdx = (totalLines > maxLines) ? currentLine % maxLines : 0;
        int numLinesToReturn = (totalLines < maxLines) ? totalLines : maxLines;

        for (int i = 0; i < numLinesToReturn; i++) {
            int idx = (startIdx + i) % maxLines;
            if (i > 0) result += ",";

            // Escape quotes and backslashes in log line
            String escapedLine = lines[idx];
            escapedLine.replace("\\", "\\\\");
            escapedLine.replace("\"", "\\\"");

            result += "\"" + escapedLine + "\"";
        }
        result += "]";

        lineCount = numLinesToReturn;
        delete[] lines;
        return true;
    });

    if (!success) {
        return "{\"error\":\"Failed to read log entries\"}";
    }

    return result;
}

void SDLogger::processRecentLogEntries(int maxLines, std::function<void(const String&)> processor) {
    if (!_initialized) {
        processor("{\"error\":\"Logger not initialized\"}");
        return;
    }

    if (maxLines == 0) return;
    if (maxLines < 0) maxLines = 200;  // treat negative as "last 200 lines"

    safeFileOperation([this, maxLines, &processor]() {
        File file = SD_MMC.open(_currentLogFile.c_str(), FILE_READ);
        if (!file) {
            processor("error - Failed to open log file");
            return false;
        }

        // For limited lines, count total first then skip to the last N
        int totalLines = 0;
        while (file.available()) {
            file.readStringUntil('\n');
            totalLines++;
        }

        int linesToSkip = (totalLines > maxLines) ? (totalLines - maxLines) : 0;

        file.seek(0);
        int currentLine = 0;

        while (file.available()) {
            String line = file.readStringUntil('\n');
            if (currentLine >= linesToSkip && line.length() > 0) {
                line.replace("\\", "\\\\");
                line.replace("\"", "\\\"");
                processor(line);
            }
            currentLine++;
        }

        file.close();
        return true;
    });
}

std::vector<String> SDLogger::listLogFilesInternal() {
    std::vector<String> logFiles;

    File dir = SD_MMC.open(_logDir);
    if (!dir || !dir.isDirectory()) {
        return logFiles;
    }

    File entry;
    while ((entry = dir.openNextFile())) {
        String name = entry.name();
        if (name.endsWith(".log")) {
            logFiles.push_back(name);
        }
        entry.close();
    }
    dir.close();

    // Sort alphabetically (which also sorts by boot number due to naming convention)
    std::sort(logFiles.begin(), logFiles.end());

    return logFiles;
}

std::vector<String> SDLogger::listLogFiles() {
    std::vector<String> result;
    safeFileOperation([this, &result]() -> bool {
        result = listLogFilesInternal();
        return true;
    });
    return result;
}

void SDLogger::processLogFile(const String& filename, int maxLines, std::function<void(const String&)> processor) {
    if (!_initialized) {
        processor("error - Logger not initialized");
        return;
    }

    String fullPath = _logDir + "/" + filename;

    safeFileOperation([this, &fullPath, maxLines, &processor]() {
        File file = SD_MMC.open(fullPath.c_str(), FILE_READ);
        if (!file) {
            processor("error - Failed to open log file");
            return false;
        }

        if (maxLines == -1) {
            // Process all lines from start to end
            while (file.available()) {
                String line = file.readStringUntil('\n');
                if (line.length() > 0) {
                    line.replace("\\", "\\\\");
                    line.replace("\"", "\\\"");
                    processor(line);
                }
            }
            file.close();
            return true;
        }

        // For limited lines, count total first then skip to the last N
        int totalLines = 0;
        while (file.available()) {
            file.readStringUntil('\n');
            totalLines++;
        }

        int linesToSkip = (totalLines > maxLines) ? (totalLines - maxLines) : 0;

        file.seek(0);
        int currentLine = 0;

        while (file.available()) {
            String line = file.readStringUntil('\n');
            if (currentLine >= linesToSkip && line.length() > 0) {
                line.replace("\\", "\\\\");
                line.replace("\"", "\\\"");
                processor(line);
            }
            currentLine++;
        }

        file.close();
        return true;
    });
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

    // Non-blocking enqueue with short timeout
    BaseType_t result = xQueueSend(_logQueue, &entry, pdMS_TO_TICKS(10));

    if (result == pdTRUE) {
        _totalEnqueued++;
        return true;
    } else {
        // Queue full — drop newest message (preserves earlier diagnostic context)
        _droppedCount++;

        // Rate-limit "queue full" warnings to once per second
        uint32_t now = millis();
        if (now - _lastDropWarning > 1000) {
            Serial.printf("SDLogger: Dropped %u log entries (queue full)\n", _droppedCount);
            _lastDropWarning = now;
        }
        return false;
    }
}

void SDLogger::processLogEntry(const LogEntry& entry) {
    String logEntry = formatLogEntry(entry.level, entry.message);
    writeToFile(logEntry.c_str());
    _totalWritten++;
}

void SDLogger::writerTaskFunction(void* parameter) {
    SDLogger* logger = static_cast<SDLogger*>(parameter);
    LogEntry entry;

    while (!logger->_shutdownRequested) {
        // Wait for a log entry (with 100ms timeout to check shutdown flag)
        if (xQueueReceive(logger->_logQueue, &entry, pdMS_TO_TICKS(100)) == pdTRUE) {
            logger->processLogEntry(entry);

            // Batch additional queued entries for efficiency
            int batchCount = 1;
            while (batchCount < SDLogger::BATCH_SIZE &&
                   xQueueReceive(logger->_logQueue, &entry, 0) == pdTRUE) {
                logger->processLogEntry(entry);
                batchCount++;
            }

            // Yield to allow other tasks to run
            taskYIELD();
        }
    }

    // Drain remaining queue entries before shutdown
    while (xQueueReceive(logger->_logQueue, &entry, 0) == pdTRUE) {
        logger->processLogEntry(entry);
    }

    // Signal that we're done
    if (logger->_writerExitSemaphore != nullptr) {
        xSemaphoreGive(logger->_writerExitSemaphore);
    }

    vTaskDelete(NULL);
}

void SDLogger::shutdown() {
    if (_writerTask == nullptr) {
        return;
    }

    info("SDLogger: Shutting down async writer...");

    // Signal shutdown
    _shutdownRequested = true;

    // Wait for task to finish draining and exit (5 second timeout)
    if (_writerExitSemaphore != nullptr) {
        if (xSemaphoreTake(_writerExitSemaphore, pdMS_TO_TICKS(5000)) != pdTRUE) {
            Serial.println("SDLogger: Shutdown timeout, writer task may not have exited cleanly");
        }
    }

    // Cleanup
    if (_logQueue != nullptr) {
        vQueueDelete(_logQueue);
        _logQueue = nullptr;
    }

    if (_writerExitSemaphore != nullptr) {
        vSemaphoreDelete(_writerExitSemaphore);
        _writerExitSemaphore = nullptr;
    }

    _writerTask = nullptr;

    Serial.printf("SDLogger: Shutdown complete. Total enqueued: %u, written: %u, dropped: %u\n",
                  _totalEnqueued, _totalWritten, _droppedCount);
}

uint32_t SDLogger::getQueueDepth() const {
    if (_logQueue == nullptr) {
        return 0;
    }
    return (uint32_t)uxQueueMessagesWaiting(_logQueue);
}
