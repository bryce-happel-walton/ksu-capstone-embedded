#pragma once

#include <stdint.h>
#include <stdbool.h>

#define WIFI_SSID "Speed Sign Test"
#define WIFI_PASS "ksu_capstone_giga#oreo"
#define WIFI_CHANNEL 1

#define RADAR_DATA_URI "data"

#define IMAGE_STREAM_URI "image_stream"

#define WS_INPUT_URI "ws_input"

#define LED_PANEL_ROWS 16
#define LED_PANEL_COLS 16
#define LED_PANELS_WIDE 2
#define LED_PANELS_HIGH 2

#define PIXELS (LED_PANEL_ROWS * LED_PANEL_COLS * LED_PANELS_WIDE * LED_PANELS_HIGH)

typedef enum
{
    DISPLAY_PATTERN_CORNERS,
    DISPLAY_PATTERN_CENTERS,
    DISPLAY_PATTERN_ENCIRCLE,
    DISPLAY_PATTERN_SHRINK_ENCIRCLE,
    DISPLAY_PATTERN_SHRINK_SQUARE,
    DISPLAY_PATTERN_FONT_TEST,
    DISPLAY_PATTERN_FONT_NUM_SEGMENT_TEST,
    DISPLAY_PATTERN_SLOW_DOWN,
} TestDisplayPattern;

typedef struct __attribute__((packed))
{
    char hello[32];
    uint32_t beep;
    bool boop;
} TestData;

typedef struct __attribute__((packed))
{
    TestDisplayPattern display_pattern;
} InputData;

typedef struct __attribute__((packed))
{
    uint8_t packet_type;
    uint8_t sub_type;
    uint8_t data[32];
    uint8_t data_len;
} RadarData;
