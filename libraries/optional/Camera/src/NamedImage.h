#ifndef CATCAM_NAMEDIMAGE_H
#define CATCAM_NAMEDIMAGE_H

#include <Arduino.h>

struct NamedImage {
    String filename;
    uint8_t* image;
    size_t size;

    NamedImage() : filename(""), image(nullptr), size(0) {}
};

#endif
