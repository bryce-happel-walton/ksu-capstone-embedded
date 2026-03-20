#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_camera.h"

#define WIFI_SSID "Speed Sign Test"
#define WIFI_PASS "ksu_capstone_giga#oreo"
#define WIFI_CHANNEL 1

typedef struct __attribute__((packed))
{
    char hello[32];
    uint32_t beep;
    bool boop;
} TestData;

const pixformat_t PIX_FORMAT = PIXFORMAT_JPEG;
