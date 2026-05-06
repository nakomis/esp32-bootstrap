#ifndef CATCAM_PCF8574MANAGER_H
#define CATCAM_PCF8574MANAGER_H

#include <Arduino.h>
#include <Wire.h>
#include "../../SDLogger/src/SDLogger.h"

class PCF8574Manager
{
private:
    uint8_t _i2cAddress;
    uint8_t _currentState;
    uint8_t _lastKnownState;
    bool _initialized;
    bool _connected;
    bool _emergencyMode;
    unsigned long _lastCommunication;
    uint8_t _lastError;
    uint8_t _consecutiveErrors;

    // Safety constants
    static constexpr unsigned long MAX_COMMUNICATION_TIMEOUT_MS = 5000;  // 5 seconds
    static constexpr uint8_t MAX_CONSECUTIVE_ERRORS = 3;
    static constexpr unsigned long SELF_TEST_INTERVAL_MS = 30000;        // 30 seconds

    // Error codes
    static constexpr uint8_t ERROR_NONE = 0;
    static constexpr uint8_t ERROR_I2C_TIMEOUT = 1;
    static constexpr uint8_t ERROR_I2C_NACK = 2;
    static constexpr uint8_t ERROR_DEVICE_NOT_FOUND = 3;
    static constexpr uint8_t ERROR_EMERGENCY_MODE = 4;
    static constexpr uint8_t ERROR_CONSECUTIVE_FAILURES = 5;

    // Internal I2C operations
    bool writeToDevice(uint8_t data);
    bool readFromDevice(uint8_t& data);
    bool validatePin(uint8_t pin);
    bool isActiveLowPin(uint8_t pin);
    void updateConnectionStatus();
    void logError(const char* operation, uint8_t errorCode);

public:
    // Pin definitions for PCF8574 (P0-P7)
    static constexpr uint8_t PIR_SENSOR_PIN = 0;        // P0 - PIR motion sensor (active HIGH)
    static constexpr uint8_t LIGHT_SENSOR_PIN = 1;    // P1 - Ambient Light sensor
    static constexpr uint8_t BUTTON_PIN = 2;            // P2 - Input button
    static constexpr uint8_t ATOMIZER_PIN = 3;          // P3 - Atomizer control
    static constexpr uint8_t PRESSURE_SENSOR_PIN = 4;   // P4 - Pressure sensor input
    static constexpr uint8_t LED_STRIP_1_PIN = 5;       // P5 - Flash LED Strip 1
    static constexpr uint8_t LED_STRIP_2_PIN = 6;       // P6 - Flash LED Strip 2
    static constexpr uint8_t FLASH_LED_PIN = 7;         // P7 - Single Flash LED

    // Pin sets: define once, get both array and bitmask automatically
    template<uint8_t... Pins>
    struct PinSet {
        static constexpr uint8_t pins[] = { Pins... };
        static constexpr size_t count = sizeof...(Pins);
        static constexpr uint8_t mask = ((1 << Pins) | ...);
    };
    using LowActivatedPins = PinSet<ATOMIZER_PIN, LED_STRIP_1_PIN, LED_STRIP_2_PIN>;

    // Input pins must be written HIGH for PCF8574 quasi-bidirectional I/O to work
    using InputPins = PinSet<PIR_SENSOR_PIN, LIGHT_SENSOR_PIN, BUTTON_PIN, PRESSURE_SENSOR_PIN>;

    // Initial state: active-low outputs HIGH (inactive) + input pins HIGH (readable)
    static constexpr uint8_t PCF8574_INITIAL_PIN_STATE = LowActivatedPins::mask | InputPins::mask;

    PCF8574Manager(uint8_t i2cAddress);
    bool init(int sdaPin, int sclPin);

    // Safe pin control with validation
    bool setPinState(uint8_t pin, bool state);
    bool getPinState(uint8_t pin);
    bool readPinInput(uint8_t pin);

    // Specialized control functions
    bool setAtomizerState(bool active);
    bool setFlashLED(bool on);
    bool setLedStrip(bool on);
    bool readPressureSensor();
    bool readPIRSensor();

    // System health and safety
    bool isConnected();
    bool performSelfTest();
    uint8_t getLastError();
    void resetErrorState();

    // Safety features
    void emergencyShutdown();
    bool isSafeToOperate();


};

#endif
