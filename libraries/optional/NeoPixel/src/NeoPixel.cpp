#include "NeoPixel.h"
#include <cstdint>

// Return the singleton instance
NeoPixel& NeoPixel::instance()
{
    static NeoPixel instance;
    return instance;
}

// Private constructor
NeoPixel::NeoPixel()
    : rgbLed(NUM_LEDS, RGB_LED_PIN, NEO_GRB + NEO_KHZ800)
{
    rgbLed.begin();
}

void NeoPixel::setBrightness(uint8_t b)
{
    rgbLed.setBrightness(b);
}

// Public API
void NeoPixel::setLedColor(uint8_t r, uint8_t g, uint8_t b)
{
    rgbLed.setPixelColor(0, rgbLed.Color(r, g, b));
    rgbLed.show();
}