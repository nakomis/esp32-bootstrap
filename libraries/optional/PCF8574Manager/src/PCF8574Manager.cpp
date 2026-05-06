#include "PCF8574Manager.h"

PCF8574Manager::PCF8574Manager(uint8_t i2cAddress)
    : _i2cAddress(i2cAddress), _currentState(0x00), _lastKnownState(0x00),
    _initialized(false), _connected(false), _emergencyMode(false),
    _lastCommunication(0), _lastError(ERROR_NONE), _consecutiveErrors(0) {
}

bool PCF8574Manager::init(int sdaPin, int sclPin) {
    // Initialize I2C with custom pins (UART0 pins)
    Wire.begin(sdaPin, sclPin);
    Wire.setClock(100000); // 100kHz for reliability

    SDLogger::getInstance().infof("PCF8574Manager initializing on I2C address 0x%02X (SDA: %d, SCL: %d)",
        _i2cAddress, sdaPin, sclPin);

    // Test connection
    if (!isConnected()) {
        SDLogger::getInstance().errorf("PCF8574 device not found at address 0x%02X", _i2cAddress);
        _lastError = ERROR_DEVICE_NOT_FOUND;
        return false;
    }

    // Initialize all pins to safe state (LOW/OFF) with the exception of the low activated pins
    _currentState = PCF8574_INITIAL_PIN_STATE;
    if (!writeToDevice(_currentState)) {
        SDLogger::getInstance().errorf("Failed to initialize PCF8574 to safe state");
        return false;
    }

    // Perform self-test
    if (!performSelfTest()) {
        SDLogger::getInstance().errorf("PCF8574 self-test failed");
        return false;
    }

    _initialized = true;
    _lastCommunication = millis();

    SDLogger::getInstance().infof("PCF8574Manager initialized successfully (SAFETY MODE: Emergency shutdown capable)");
    SDLogger::getInstance().infof("Pin assignments: Atomizer=P%d, Flash=P%d, Pressure=P%d",
        ATOMIZER_PIN, FLASH_LED_PIN, PRESSURE_SENSOR_PIN);

    return true;
}

bool PCF8574Manager::setPinState(uint8_t pin, bool state) {
    if (!validatePin(pin)) {
        return false;
    }

    if (!isSafeToOperate()) {
        logError("setPinState", ERROR_EMERGENCY_MODE);
        return false;
    }

    bool stateToSet = isActiveLowPin(pin) ? !state : state;

    // Update the bit for this pin
    if (stateToSet) {
        _currentState |= (1 << pin);
    }
    else {
        _currentState &= ~(1 << pin);
    }

    // Write to device
    if (!writeToDevice(_currentState)) {
        logError("setPinState", _lastError);
        return false;
    }

    _lastKnownState = _currentState;
    return true;
}

bool PCF8574Manager::getPinState(uint8_t pin) {
    if (!validatePin(pin)) {
        return false;
    }

    bool rawState = (_currentState & (1 << pin)) != 0;
    return isActiveLowPin(pin) ? !rawState : rawState;
}

bool PCF8574Manager::readPinInput(uint8_t pin) {

    if (!validatePin(pin)) {
        return false;
    }

    if (!isSafeToOperate()) {
        return false;
    }

    uint8_t data;
    if (!readFromDevice(data)) {
        return false;
    }

    return (data & (1 << pin)) != 0;
}

bool PCF8574Manager::setAtomizerState(bool active) {
    // CRITICAL SAFETY: Log all atomizer state changes
    SDLogger::getInstance().criticalf("PCF8574: Atomizer state change requested: %s", active ? "ACTIVE" : "INACTIVE");

    if (active && !isSafeToOperate()) {
        SDLogger::getInstance().errorf("PCF8574: Atomizer activation BLOCKED - system not safe to operate");
        return false;
    }

    bool result = setPinState(ATOMIZER_PIN, active);

    if (result) {
        SDLogger::getInstance().criticalf("PCF8574: Atomizer state changed to: %s", active ? "ACTIVE" : "INACTIVE");
    }
    else {
        SDLogger::getInstance().errorf("PCF8574: FAILED to change atomizer state to: %s", active ? "ACTIVE" : "INACTIVE");
    }

    return result;
}

bool PCF8574Manager::setFlashLED(bool on) {
    return setPinState(FLASH_LED_PIN, on);
}

bool PCF8574Manager::setLedStrip(bool on) {
    return setPinState(LED_STRIP_1_PIN, on);
}

bool PCF8574Manager::readPressureSensor() {
    return readPinInput(PRESSURE_SENSOR_PIN);
}

bool PCF8574Manager::readPIRSensor() {
    return readPinInput(PIR_SENSOR_PIN);
}

bool PCF8574Manager::isConnected() {
    Wire.beginTransmission(_i2cAddress);
    uint8_t error = Wire.endTransmission();

    _connected = (error == 0);
    updateConnectionStatus();

    return _connected;
}

bool PCF8574Manager::performSelfTest() {
    SDLogger::getInstance().infof("PCF8574: Performing self-test...");

    // Test pattern: alternating bits
    uint8_t testPatterns[] = { 0x55, 0xAA, 0x00, 0xFF };

    for (uint8_t pattern : testPatterns) {
        if (!writeToDevice(pattern)) {
            SDLogger::getInstance().errorf("PCF8574: Self-test failed on write pattern 0x%02X", pattern);
            return false;
        }

        delay(10); // Small delay for stability

        uint8_t readback;
        if (!readFromDevice(readback)) {
            SDLogger::getInstance().errorf("PCF8574: Self-test failed on read pattern 0x%02X", pattern);
            return false;
        }

        // Note: PCF8574 pins configured as outputs will read back as written
        // Pins configured as inputs (high) will reflect actual pin state
    }

    // Return to safe/inactive state (active-low pins HIGH, active-high pins LOW)
    _currentState = PCF8574_INITIAL_PIN_STATE;
    if (!writeToDevice(_currentState)) {
        SDLogger::getInstance().errorf("PCF8574: Self-test failed to return to safe state");
        return false;
    }

    SDLogger::getInstance().infof("PCF8574: Self-test completed successfully");
    return true;
}

uint8_t PCF8574Manager::getLastError() {
    return _lastError;
}

void PCF8574Manager::resetErrorState() {
    _lastError = ERROR_NONE;
    _consecutiveErrors = 0;
    _emergencyMode = false;

    SDLogger::getInstance().infof("PCF8574: Error state reset");
}

void PCF8574Manager::emergencyShutdown() {
    SDLogger::getInstance().criticalf("PCF8574: *** EMERGENCY SHUTDOWN ACTIVATED ***");

    _emergencyMode = true;

    // Force all outputs to safe/inactive state (active-low pins HIGH, active-high pins LOW)
    _currentState = PCF8574_INITIAL_PIN_STATE;

    // Attempt to write safe state, but don't fail if I2C is broken
    writeToDevice(_currentState);

    SDLogger::getInstance().criticalf("PCF8574: All outputs forced to inactive state");
}

bool PCF8574Manager::isSafeToOperate() {
    if (_emergencyMode) {
        return false;
    }

    if (!_initialized) {
        return false;
    }

    if (_consecutiveErrors >= MAX_CONSECUTIVE_ERRORS) {
        SDLogger::getInstance().errorf("PCF8574: Too many consecutive errors (%d), entering emergency mode", _consecutiveErrors);
        emergencyShutdown();
        return false;
    }

    // // Check communication timeout
    // if ((millis() - _lastCommunication) > MAX_COMMUNICATION_TIMEOUT_MS) {
    //     SDLogger::getInstance().errorf("PCF8574: Communication timeout, entering emergency mode");
    //     emergencyShutdown();
    //     return false;
    // }

    return true;
}

bool PCF8574Manager::isActiveLowPin(uint8_t pin) {
    const uint8_t* found = std::find(std::begin(LowActivatedPins::pins), std::end(LowActivatedPins::pins), pin);
    return found != std::end(LowActivatedPins::pins);
}

bool PCF8574Manager::writeToDevice(uint8_t data) {
    Wire.beginTransmission(_i2cAddress);
    Wire.write(data);
    uint8_t error = Wire.endTransmission();

    if (error == 0) {
        _lastCommunication = millis();
        _consecutiveErrors = 0;
        _lastError = ERROR_NONE;
        return true;
    }
    else {
        _consecutiveErrors++;

        switch (error) {
        case 2:
            _lastError = ERROR_I2C_NACK;
            logError("writeToDevice", ERROR_I2C_NACK);
            break;
        case 5:
            _lastError = ERROR_I2C_TIMEOUT;
            logError("writeToDevice", ERROR_I2C_TIMEOUT);
            break;
        default:
            _lastError = error;
            logError("writeToDevice", error);
            break;
        }

        return false;
    }
}

bool PCF8574Manager::readFromDevice(uint8_t& data) {
    uint8_t bytesReceived = Wire.requestFrom(_i2cAddress, (uint8_t)1);

    if (bytesReceived == 1) {
        data = Wire.read();
        _lastCommunication = millis();
        _consecutiveErrors = 0;
        _lastError = ERROR_NONE;
        return true;
    }
    else {
        _consecutiveErrors++;
        _lastError = ERROR_I2C_TIMEOUT;
        logError("readFromDevice", ERROR_I2C_TIMEOUT);
        return false;
    }
}

bool PCF8574Manager::validatePin(uint8_t pin) {
    if (pin > 7) {
        SDLogger::getInstance().errorf("PCF8574: Invalid pin number %d (must be 0-7)", pin);
        return false;
    }
    return true;
}

void PCF8574Manager::updateConnectionStatus() {
    if (!_connected && _consecutiveErrors == 0) {
        _consecutiveErrors = 1; // Mark first connection failure
    }
}

void PCF8574Manager::logError(const char* operation, uint8_t errorCode) {
    const char* errorMsg;

    switch (errorCode) {
    case ERROR_I2C_TIMEOUT:
        errorMsg = "I2C timeout";
        break;
    case ERROR_I2C_NACK:
        errorMsg = "I2C NACK";
        break;
    case ERROR_DEVICE_NOT_FOUND:
        errorMsg = "Device not found";
        break;
    case ERROR_EMERGENCY_MODE:
        errorMsg = "Emergency mode active";
        break;
    case ERROR_CONSECUTIVE_FAILURES:
        errorMsg = "Too many consecutive failures";
        break;
    default:
        errorMsg = "Unknown error";
        break;
    }

    SDLogger::getInstance().errorf("PCF8574: %s failed - %s (code: %d, consecutive: %d)",
        operation, errorMsg, errorCode, _consecutiveErrors);
}
