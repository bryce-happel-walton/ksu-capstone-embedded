#pragma once

#include "led_strip.h"
#include "shared_lib.h"

#define MIN_LED_RESET_TIME_MS 5

extern volatile TestDisplayPattern current_display_pattern;

led_strip_handle_t configure_led(void);
int led_index(int row, int col);
void led_task(void *pvParameters);
