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
    DISPLAY_PATTERN_SPEED,
} DisplayPattern;

typedef enum
{
    MPH,
    KPH,
} SpeedUnit;

typedef struct __attribute__((packed))
{
    DisplayPattern display_pattern;
    SpeedUnit display_speed_unit;
    int speed_threshold_kph;
} InputData;

typedef struct __attribute__((packed))
{
    int8_t angle;      // degrees  (reported value -0x80 for actual)
    uint8_t distance;  // meters
    uint8_t direction; // 0 - away | 1 - towards  ?? might be reversed pdf table shows 0 away, example uses 0 towards
    uint8_t speed;     // km/h
    uint8_t snr;       // signal/noise
} VehicleTarget;

#define MAX_RADAR_TARGETS 3

typedef struct __attribute__((packed))
{
    uint8_t count;
    VehicleTarget targets[MAX_RADAR_TARGETS];
} RadarPayload;
