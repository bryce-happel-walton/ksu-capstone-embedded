#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "esp_log.h"

#include "pindefs.h"
#include "radar.h"

#define RADAR_UART_NUM UART_NUM_1
#define RADAR_BAUD_RATE 115200
#define RADAR_BUF_SIZE 1024

// Data frame: F4 F3 F2 F1  [len_lo] [len_hi]  [payload × datalength]  F8 F7 F6 F5
// Payload:    [alarm_info] [target_count] [target × N (5 bytes each)] [optional tail/check]
static const uint8_t FRAME_HEADER[4] = {0xF4, 0xF3, 0xF2, 0xF1};
static const uint8_t FRAME_FOOTER[4] = {0xF8, 0xF7, 0xF6, 0xF5};
#define FRAME_MAX_PAYLOAD 64

static const char *TAG = "radar";

volatile RadarPayload latest_radar_payload = {0};

esp_err_t radar_init(void)
{
    uart_config_t uart_config = {
        .baud_rate = RADAR_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
    };

    esp_err_t ret = uart_driver_install(RADAR_UART_NUM, RADAR_BUF_SIZE, 0, 0, NULL, 0);
    if (ret != ESP_OK)
    {
        return ret;
    }

    ret = uart_param_config(RADAR_UART_NUM, &uart_config);
    if (ret != ESP_OK)
    {
        return ret;
    }

    ret = uart_set_pin(RADAR_UART_NUM, RADAR_TX, RADAR_RX, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (ret != ESP_OK)
    {
        return ret;
    }

    ESP_LOGI(TAG, "UART initialized on TX=%d RX=%d at %d baud", RADAR_TX, RADAR_RX, RADAR_BAUD_RATE);
    return ESP_OK;
}

static bool sync_to_header(void)
{
    int state = 0;
    uint8_t byte;

    while (state < 4)
    {
        if (uart_read_bytes(RADAR_UART_NUM, &byte, 1, pdMS_TO_TICKS(200)) != 1)
        {
            return false;
        }

        if (byte == FRAME_HEADER[state])
        {
            state++;
        }
        else if (byte == FRAME_HEADER[0])
        {
            state = 1;
        }
        else
        {
            state = 0;
        }
    }
    return true;
}

static int read_data_frame(uint8_t *payload, size_t max_len)
{
    if (!sync_to_header())
    {
        return -1;
    }

    uint8_t len_bytes[2];
    if (uart_read_bytes(RADAR_UART_NUM, len_bytes, 2, pdMS_TO_TICKS(100)) < 2)
    {
        return -1;
    }

    uint16_t datalength = ((uint16_t)len_bytes[1] << 8) | len_bytes[0];
    if (datalength > max_len)
    {
        ESP_LOGW(TAG, "Datalength %u exceeds buffer", datalength);
        return -1;
    }

    if (datalength > 0 && uart_read_bytes(RADAR_UART_NUM, payload, datalength, pdMS_TO_TICKS(100)) < datalength)
    {
        return -1;
    }

    uint8_t footer[4];
    if (uart_read_bytes(RADAR_UART_NUM, footer, 4, pdMS_TO_TICKS(100)) < 4)
    {
        return -1;
    }
    if (memcmp(footer, FRAME_FOOTER, 4) != 0)
    {
        ESP_LOGW(TAG, "Bad footer %02x %02x %02x %02x", footer[0], footer[1], footer[2], footer[3]);
        return -1;
    }

    return (int)datalength;
}

static void log_payload(const RadarPayload *payload)
{
    if (payload->count == 0)
    {
        ESP_LOGI(TAG, "frame: no targets");
        return;
    }

    ESP_LOGI(TAG, "frame: %u target(s)", payload->count);
    for (int i = 0; i < payload->count; i++)
    {
        const VehicleTarget *t = &payload->targets[i];
        ESP_LOGI(TAG, "  [%d] angle=%d dist=%um dir=%s speed=%ukm/h snr=%u",
                 i, t->angle, t->distance,
                 t->direction == 0 ? "away" : "toward",
                 t->speed, t->snr);
    }
}

static RadarPayload parse_targets(const uint8_t *payload, uint16_t datalength)
{
    RadarPayload result = {0};
    if (datalength < 2)
    {
        return result;
    }

    uint8_t count = payload[1];
    if (2 + count * sizeof(VehicleTarget) > datalength)
    {
        ESP_LOGW(TAG, "Target count %u inconsistent with datalength %u", count, datalength);
        return result;
    }
    if (count > MAX_RADAR_TARGETS)
    {
        count = MAX_RADAR_TARGETS;
    }

    for (int i = 0; i < count; i++)
    {
        const uint8_t *p = &payload[2 + i * sizeof(VehicleTarget)];
        result.targets[i].angle = (int8_t)(p[0] - 0x80);
        result.targets[i].distance = p[1];
        result.targets[i].direction = p[2];
        result.targets[i].speed = p[3];
        result.targets[i].snr = p[4];
    }
    result.count = count;
    return result;
}

void radar_task(void *pvParameters)
{
    uint8_t payload[FRAME_MAX_PAYLOAD];

    while (true)
    {
        int datalength = read_data_frame(payload, sizeof(payload));
        if (datalength < 0)
        {
            continue;
        }

        RadarPayload parsed = parse_targets(payload, (uint16_t)datalength);
        log_payload(&parsed);
        latest_radar_payload = parsed;
    }
}
