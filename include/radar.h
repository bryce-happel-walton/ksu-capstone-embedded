#pragma once

#include <stdint.h>
#include "esp_err.h"
#include "shared_lib.h"

extern volatile RadarPayload latest_radar_payload;

esp_err_t radar_init(void);
void radar_task(void *pvParameters);
