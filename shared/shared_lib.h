#pragma once

#include <stdint.h>
#include <stdbool.h>

#define WIFI_SSID "Speed Sign Test"
#define WIFI_PASS "ksu_capstone_giga#oreo"
#define WIFI_CHANNEL 1

#define TEST_DATA_URI "data"
#define TEST_DATA_PORT 80

#define IMAGE_STREAM_URI "image_stream"
#define IMAGE_STREAM_PORT 81

#define LED_PANEL_ROWS 16
#define LED_PANEL_COLS 16
#define LED_PANELS 4
const uint32_t PIXELS = LED_PANEL_ROWS * LED_PANEL_COLS * LED_PANELS;

typedef struct __attribute__((packed))
{
    char hello[32];
    uint32_t beep;
    bool boop;
} TestData;
