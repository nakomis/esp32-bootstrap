#pragma once

#include <Arduino.h>
#include <functional>

// Only include NeoPixel for ESP32-S3 builds
#ifdef ESP32S3_CAM
#include "NeoPixel.h"
#endif

/**
 * LedController - Manages RGB LED operations
 *
 * Provides a unified interface for LED control including colors, brightness,
 * and animated patterns like accelerating flashes for countdowns.
 *
 * Note: On non-ESP32S3 boards, LED operations are no-ops.
 */
class LedController {
public:
    // Callback type for checking if an animation should be cancelled
    using CancelCheckCallback = std::function<bool()>;

    // Callback type for background tasks during animations
    using LoopCallback = std::function<void()>;

    /**
     * Initialize the LED controller
     * @param defaultBrightness Initial brightness (0-255)
     */
    void init(uint8_t defaultBrightness = 100);

    /**
     * Run LED test sequence (RGB cycle)
     * @param cycles Number of RGB cycles to run
     * @param delayMs Delay between color changes in ms
     */
    void runTestSequence(int cycles = 3, int delayMs = 100);

    /**
     * Set LED color
     * @param r Red component (0-255)
     * @param g Green component (0-255)
     * @param b Blue component (0-255)
     */
    void setColor(uint8_t r, uint8_t g, uint8_t b);

    /**
     * Turn LED off
     */
    void off();

    /**
     * Set LED brightness
     * @param brightness Brightness level (0-255)
     */
    void setBrightness(uint8_t brightness);

    /**
     * Get current brightness setting
     * @return Current brightness (0-255)
     */
    uint8_t getBrightness() const { return _brightness; }

    /**
     * Flash LED with accelerating frequency (countdown effect)
     *
     * The flash interval interpolates from startInterval to endInterval over
     * the duration, creating a "speeding up" effect useful for countdowns.
     *
     * @param r Red component
     * @param g Green component
     * @param b Blue component
     * @param startInterval Initial flash interval in ms
     * @param endInterval Final flash interval in ms
     * @param durationMs Total duration of the animation
     * @param cancelCheck Optional callback to check if animation should cancel (returns true to cancel)
     * @param loopCallback Optional callback for background tasks during animation
     * @return true if cancelled via cancelCheck, false if completed normally
     */
    bool flashAccelerating(uint8_t r, uint8_t g, uint8_t b,
                          unsigned long startInterval, unsigned long endInterval,
                          unsigned long durationMs,
                          CancelCheckCallback cancelCheck = nullptr,
                          LoopCallback loopCallback = nullptr);

    /**
     * Brief green flash to indicate success
     * @param durationMs How long to show green (default 500ms)
     */
    void flashSuccess(int durationMs = 500);

    /**
     * Brief red flash to indicate error
     * @param durationMs How long to show red (default 500ms)
     */
    void flashError(int durationMs = 500);

private:
    uint8_t _brightness = 100;
    bool _initialized = false;
};
