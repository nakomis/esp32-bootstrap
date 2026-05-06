#include "LedController.h"
#include <SDLogger.h>

void LedController::init(uint8_t defaultBrightness) {
#ifdef ESP32S3_CAM
    SDLogger::getInstance().infof("Initializing RGB LED on GPIO %d", RGB_LED_PIN);
    _brightness = defaultBrightness;
    NeoPixel::instance().setBrightness(255);  // Start at full for test sequence
    _initialized = true;
#else
    SDLogger::getInstance().debugf("LED controller: no RGB LED on this board");
    _initialized = false;
#endif
}

void LedController::runTestSequence(int cycles, int delayMs) {
#ifdef ESP32S3_CAM
    if (!_initialized) return;

    // RGB cycle test
    for (int i = 0; i < cycles; i++) {
        NeoPixel::instance().setLedColor(255, 0, 0);  // Red
        delay(delayMs);

        NeoPixel::instance().setLedColor(0, 255, 0);  // Green
        delay(delayMs);

        NeoPixel::instance().setLedColor(0, 0, 255);  // Blue
        delay(delayMs);
    }

    // Turn off and set working brightness
    NeoPixel::instance().setLedColor(0, 0, 0);
    delay(250);
    NeoPixel::instance().setBrightness(_brightness);
#endif
}

void LedController::setColor(uint8_t r, uint8_t g, uint8_t b) {
#ifdef ESP32S3_CAM
    NeoPixel::instance().setLedColor(r, g, b);
#endif
}

void LedController::off() {
    setColor(0, 0, 0);
}

void LedController::setBrightness(uint8_t brightness) {
    _brightness = brightness;
#ifdef ESP32S3_CAM
    NeoPixel::instance().setBrightness(brightness);
#endif
}

bool LedController::flashAccelerating(uint8_t r, uint8_t g, uint8_t b,
                                      unsigned long startInterval, unsigned long endInterval,
                                      unsigned long durationMs,
                                      CancelCheckCallback cancelCheck,
                                      LoopCallback loopCallback) {
    unsigned long startTime = millis();
    unsigned long elapsed = 0;
    bool ledOn = false;
    int toggleCount = 0;

    SDLogger::getInstance().debugf("Flash LED: color=(%d,%d,%d) start=%lu end=%lu duration=%lu",
        r, g, b, startInterval, endInterval, durationMs);

    unsigned long lastToggle = startTime;

    while (elapsed < durationMs) {
        elapsed = millis() - startTime;

        // Check for cancellation
        if (cancelCheck && cancelCheck()) {
            off();
            SDLogger::getInstance().debugf("Flash cancelled");
            return true;
        }

        // Calculate current interval - interpolate from startInterval to endInterval
        float progress = (float)elapsed / (float)durationMs;
        unsigned long currentInterval = startInterval + (unsigned long)(progress * (float)(endInterval - startInterval));

        // Clamp interval to reasonable values
        if (currentInterval < 30) currentInterval = 30;
        if (currentInterval > 1000) currentInterval = 1000;

        if (millis() - lastToggle >= currentInterval) {
            ledOn = !ledOn;
            toggleCount++;
            if (ledOn) {
                setColor(r, g, b);
            } else {
                off();
            }
            lastToggle = millis();
        }

        // Run background tasks
        if (loopCallback) {
            loopCallback();
        }

        delay(10);  // Small delay to prevent tight loop
    }

    SDLogger::getInstance().debugf("Flash complete: %d toggles", toggleCount);
    off();
    return false;
}

void LedController::flashSuccess(int durationMs) {
    setColor(0, 255, 0);
    delay(durationMs);
    off();
}

void LedController::flashError(int durationMs) {
    setColor(255, 0, 0);
    delay(durationMs);
    off();
}
