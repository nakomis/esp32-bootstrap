#include "InputManager.h"

void InputManager::init(int bootButtonPin, unsigned long debounceMs) {
    _bootButtonPin = bootButtonPin;
    _debounceMs = debounceMs;

    // Configure button pin with internal pull-up
    pinMode(_bootButtonPin, INPUT_PULLUP);

    // Initialize state
    _lastRawState = digitalRead(_bootButtonPin) == LOW;
    _currentState = _lastRawState;
    _lastStableState = _lastRawState;
    _lastChangeTime = millis();
    _pressStartTime = 0;
    _justPressed = false;
    _justReleased = false;

    _initialized = true;
}

void InputManager::update() {
    if (!_initialized) return;

    bool rawState = digitalRead(_bootButtonPin) == LOW;

    // Check if raw state changed
    if (rawState != _lastRawState) {
        _lastChangeTime = millis();
        _lastRawState = rawState;
    }

    // If state has been stable for debounce period, accept it
    if ((millis() - _lastChangeTime) >= _debounceMs) {
        if (rawState != _currentState) {
            _currentState = rawState;

            // Detect edges
            if (_currentState && !_lastStableState) {
                // Button just pressed
                _justPressed = true;
                _pressStartTime = millis();
            } else if (!_currentState && _lastStableState) {
                // Button just released
                _justReleased = true;
                _pressStartTime = 0;
            }

            _lastStableState = _currentState;
        }
    }
}

bool InputManager::isBootButtonPressed() const {
    return _currentState;
}

bool InputManager::wasBootButtonJustPressed() {
    if (_justPressed) {
        _justPressed = false;
        return true;
    }
    return false;
}

bool InputManager::wasBootButtonJustReleased() {
    if (_justReleased) {
        _justReleased = false;
        return true;
    }
    return false;
}

unsigned long InputManager::getBootButtonHoldTime() const {
    if (_currentState && _pressStartTime > 0) {
        return millis() - _pressStartTime;
    }
    return 0;
}
