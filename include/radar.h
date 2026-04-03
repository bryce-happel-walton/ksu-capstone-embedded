#pragma once

#include <stdint.h>
#include "esp_err.h"
#include "shared_lib.h"

typedef RadarData radar_frame_t;

extern volatile RadarData latest_radar_data;

esp_err_t radar_init(void);
void radar_task(void *pvParameters);
