#pragma once

#include <Arduino.h>

/**
 * InputManager - Handles button input with debouncing
 *
 * Provides clean edge detection for button presses with configurable
 * debounce timing. Call update() in the main loop to poll button state.
 */
class InputManager {
public:
    /**
     * Initialize the input manager
     * @param bootButtonPin GPIO pin for BOOT button (default GPIO 0)
     * @param debounceMs Debounce delay in milliseconds (default 50ms)
     */
    void init(int bootButtonPin = 0, unsigned long debounceMs = 50);

    /**
     * Update button states - call this in loop()
     * Must be called regularly to detect button presses
     */
    void update();

    /**
     * Check if BOOT button is currently pressed (raw state)
     * @return true if button is pressed (LOW)
     */
    bool isBootButtonPressed() const;

    /**
     * Check if BOOT button was just pressed (rising edge, debounced)
     * Returns true only once per press, resets after reading
     * @return true if button was just pressed
     */
    bool wasBootButtonJustPressed();

    /**
     * Check if BOOT button was just released (falling edge, debounced)
     * Returns true only once per release, resets after reading
     * @return true if button was just released
     */
    bool wasBootButtonJustReleased();

    /**
     * Get how long the button has been held (in ms)
     * @return Duration in ms if pressed, 0 if not pressed
     */
    unsigned long getBootButtonHoldTime() const;

private:
    int _bootButtonPin = 0;
    unsigned long _debounceMs = 50;
    bool _initialized = false;

    // Button state tracking
    bool _lastRawState = false;
    bool _currentState = false;
    bool _lastStableState = false;
    unsigned long _lastChangeTime = 0;
    unsigned long _pressStartTime = 0;

    // Edge detection flags (cleared after reading)
    bool _justPressed = false;
    bool _justReleased = false;
};
