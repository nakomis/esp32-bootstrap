#ifndef CATCAM_NEO_PIXEL
#define CATCAM_NEW_PIXEL

#ifdef ESP32S3_CAM
#include <Adafruit_NeoPixel.h>
#define RGB_LED_PIN 48
#define NUM_LEDS 1
#endif

#include <Arduino.h>

class NeoPixel {
public:
    static NeoPixel& instance();

    void setLedColor(uint8_t r, uint8_t g, uint8_t b);
    void setBrightness(uint8_t b);

    // Delete copy/move
    NeoPixel(const NeoPixel&) = delete;
    NeoPixel& operator=(const NeoPixel&) = delete;
    NeoPixel(NeoPixel&&) = delete;
    NeoPixel& operator=(NeoPixel&&) = delete;

private:
    NeoPixel();
#ifdef ESP32S3_CAM
    Adafruit_NeoPixel rgbLed;
#endif
};

#endif // #ifndef CATCAM_NEO_PIXEL