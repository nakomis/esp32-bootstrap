#include "Camera.h"
#include "../../SDLogger/src/SDLogger.h"

Camera::Camera() {
    failureCount = 0;
}

void Camera::init(const CameraSettings& settings) {
    SDLogger::getInstance().infof("Initializing ESP32-CAM...");

    camera_config_t config;
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer = LEDC_TIMER_0;
    config.pin_d0 = Y2_GPIO_NUM;
    config.pin_d1 = Y3_GPIO_NUM;
    config.pin_d2 = Y4_GPIO_NUM;
    config.pin_d3 = Y5_GPIO_NUM;
    config.pin_d4 = Y6_GPIO_NUM;
    config.pin_d5 = Y7_GPIO_NUM;
    config.pin_d6 = Y8_GPIO_NUM;
    config.pin_d7 = Y9_GPIO_NUM;
    config.pin_xclk = XCLK_GPIO_NUM;
    config.pin_pclk = PCLK_GPIO_NUM;
    config.pin_vsync = VSYNC_GPIO_NUM;
    config.pin_href = HREF_GPIO_NUM;
    config.pin_sccb_sda = SIOD_GPIO_NUM;
    config.pin_sccb_scl = SIOC_GPIO_NUM;
    config.pin_pwdn = PWDN_GPIO_NUM;
    config.pin_reset = RESET_GPIO_NUM;
    config.xclk_freq_hz = 20000000;
    config.pixel_format = PIXFORMAT_JPEG;

    // Use settings for frame size, quality, and buffer count
    config.frame_size = (framesize_t)settings.frameSize;
    config.jpeg_quality = settings.jpegQuality;

    if (psramFound()) {
        config.fb_count = settings.fbCount;
        SDLogger::getInstance().infof("PSRAM found - frameSize=%d, quality=%d, fbCount=%d",
            settings.frameSize, settings.jpegQuality, settings.fbCount);
    } else {
        config.fb_count = 1;  // Multiple buffers require PSRAM
        SDLogger::getInstance().warnf("PSRAM not found - forcing fbCount=1");
    }

    // Initialize camera
    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        SDLogger::getInstance().errorf("Camera init failed with error 0x%x", err);
        failureCount++;
        return;
    }

    // Apply sensor settings (brightness, contrast, etc.)
    applySettings(settings);

    SDLogger::getInstance().infof("ESP32-CAM initialized successfully");
    failureCount = 0;
}

void Camera::applySettings(const CameraSettings& settings) {
    sensor_t* s = esp_camera_sensor_get();
    if (s == nullptr) {
        SDLogger::getInstance().errorf("Cannot apply camera settings - sensor not available");
        return;
    }

    s->set_framesize(s, (framesize_t)settings.frameSize);
    s->set_quality(s, settings.jpegQuality);
    // fbCount can only be set during init() - requires camera reinit
    s->set_brightness(s, settings.brightness);
    s->set_contrast(s, settings.contrast);
    s->set_saturation(s, settings.saturation);
    s->set_special_effect(s, settings.specialEffect);
    s->set_whitebal(s, settings.whiteBalance ? 1 : 0);
    s->set_awb_gain(s, settings.awbGain ? 1 : 0);
    s->set_wb_mode(s, settings.wbMode);
    s->set_exposure_ctrl(s, settings.exposureCtrl ? 1 : 0);
    s->set_aec2(s, settings.aec2 ? 1 : 0);
    s->set_ae_level(s, settings.aeLevel);
    s->set_aec_value(s, settings.aecValue);
    s->set_gain_ctrl(s, settings.gainCtrl ? 1 : 0);
    s->set_agc_gain(s, settings.agcGain);
    s->set_gainceiling(s, (gainceiling_t)settings.gainCeiling);
    s->set_bpc(s, settings.bpc ? 1 : 0);
    s->set_wpc(s, settings.wpc ? 1 : 0);
    s->set_raw_gma(s, settings.rawGma ? 1 : 0);
    s->set_lenc(s, settings.lenc ? 1 : 0);
    s->set_hmirror(s, settings.hmirror ? 1 : 0);
    s->set_vflip(s, settings.vflip ? 1 : 0);
    s->set_dcw(s, settings.dcw ? 1 : 0);
    s->set_colorbar(s, settings.colorbar ? 1 : 0);

    SDLogger::getInstance().infof("Camera settings applied");
}

NamedImage* Camera::getImage() {
    // Flush stale frames from the buffer pool
    // With fb_count=2, old frames may be sitting in the queue
    // At ~15fps, each frame takes ~66ms, so we need longer delays
    for (int i = 0; i < 4; i++) {
        camera_fb_t* stale = esp_camera_fb_get();
        if (stale) {
            esp_camera_fb_return(stale);
        }
        delay(100);  // Wait for fresh frame capture
    }

    // Now capture the actual frame
    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) {
        SDLogger::getInstance().errorf("Camera capture failed");
        failureCount++;
        return nullptr;
    }

    if (fb->len == 0) {
        SDLogger::getInstance().errorf("Camera captured empty frame");
        esp_camera_fb_return(fb);
        failureCount++;
        return nullptr;
    }
    
    // Create NamedImage with timestamp
    NamedImage* namedImage = new NamedImage();
    if (!namedImage) {
        SDLogger::getInstance().errorf("Failed to allocate NamedImage");
        esp_camera_fb_return(fb);
        return nullptr;
    }
    
    // Generate filename with timestamp
    char filename[32];
    snprintf(filename, sizeof(filename), "img_%lu.jpg", millis());
    namedImage->filename = String(filename);
    
    // Copy image data to PSRAM if available, otherwise regular heap
    namedImage->image = copyToPSRAM(fb->buf, fb->len);
    namedImage->size = fb->len;
    
    // Return the camera frame buffer
    esp_camera_fb_return(fb);
    
    SDLogger::getInstance().debugf("Captured image: %s (%d bytes)", filename, namedImage->size);
    failureCount = 0;
    
    return namedImage;
}

void Camera::deInit() {
    esp_camera_deinit();
    SDLogger::getInstance().infof("Camera deinitialized");
}

void Camera::copyImageToPSRAM(NamedImage* namedImage) {
    if (!namedImage || !namedImage->image) {
        return;
    }
    
    // Image data is already copied in getImage() method
    // This method is kept for compatibility
}

void Camera::releaseImageBuffer(NamedImage* namedImage) {
    if (!namedImage) {
        return;
    }
    
    if (namedImage->image) {
        freePSRAM(namedImage->image);
        namedImage->image = nullptr;
        namedImage->size = 0;
    }
    
    delete namedImage;
}

uint8_t* Camera::copyToPSRAM(const uint8_t* src, size_t size) {
    if (!src || size == 0) {
        return nullptr;
    }
    
    uint8_t* dest = nullptr;
    
    // Try PSRAM first if available
    if (psramFound()) {
        dest = (uint8_t*)ps_malloc(size);
        if (dest) {
            memcpy(dest, src, size);
            SDLogger::getInstance().debugf("Allocated %d bytes in PSRAM", size);
            return dest;
        }
    }
    
    // Fallback to regular heap
    dest = (uint8_t*)malloc(size);
    if (dest) {
        memcpy(dest, src, size);
        SDLogger::getInstance().debugf("Allocated %d bytes in heap", size);
    } else {
        SDLogger::getInstance().errorf("Failed to allocate %d bytes for image buffer", size);
    }
    
    return dest;
}

void Camera::freePSRAM(uint8_t* ptr) {
    if (!ptr) {
        return;
    }
    
    // Check if pointer is in PSRAM range
    if (psramFound() && heap_caps_check_integrity(MALLOC_CAP_SPIRAM, true)) {
        free(ptr); // free() works for both PSRAM and regular heap
    } else {
        free(ptr);
    }
}