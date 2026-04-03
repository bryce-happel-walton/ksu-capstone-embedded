#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "esp_log.h"

#include "pindefs.h"
#include "radar.h"

#define RADAR_UART_NUM UART_NUM_1
#define RADAR_BAUD_RATE 256000
#define RADAR_BUF_SIZE 1024

#define FRAME_START 0xC0
#define FRAME_MAX_LEN 64

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

/// Frame layout: [0xC0][frame_type][sub_type][payload_len] [data × (payload_len-1)] [xor_checksum]
static bool read_frame(uint8_t *frame_type_out, uint8_t *sub_out, uint8_t *data, uint8_t *data_len_out)
{
    uint8_t byte;

    while (true)
    {
        int n = uart_read_bytes(RADAR_UART_NUM, &byte, 1, pdMS_TO_TICKS(200));
        if (n <= 0)
        {
            return false;
        }
        if (byte == FRAME_START)
        {
            break;
        }
    }

    uint8_t header[3];
    if (uart_read_bytes(RADAR_UART_NUM, header, 3, pdMS_TO_TICKS(100)) < 3)
    {
        return false;
    }

    uint8_t frame_type = header[0];
    uint8_t sub_type = header[1];
    uint8_t payload_len = header[2]; // data bytes + 1 checksum byte

    if (payload_len == 0 || payload_len > FRAME_MAX_LEN)
    {
        return false;
    }

    uint8_t payload[FRAME_MAX_LEN];
    if (uart_read_bytes(RADAR_UART_NUM, payload, payload_len, pdMS_TO_TICKS(100)) < payload_len)
    {
        return false;
    }

    uint8_t xorval = FRAME_START ^ frame_type ^ sub_type ^ payload_len;
    for (int i = 0; i < payload_len - 1; i++)
    {
        xorval ^= payload[i];
    }

    if (xorval != payload[payload_len - 1])
    {
        ESP_LOGW(TAG, "Checksum mismatch: expected 0x%02x got 0x%02x", xorval, payload[payload_len - 1]);
        return false;
    }

    *frame_type_out = frame_type;
    *sub_out = sub_type;
    *data_len_out = payload_len - 1;
    for (int i = 0; i < *data_len_out; i++)
    {
        data[i] = payload[i];
    }

    return true;
}

static RadarPayload parse_targets(const uint8_t *data, uint8_t data_len)
{
    RadarPayload payload = {0};
    uint8_t count = data_len / sizeof(VehicleTarget);
    if (count > MAX_RADAR_TARGETS)
    {
        count = MAX_RADAR_TARGETS;
    }

    for (int i = 0; i < count; i++)
    {
        const uint8_t *p = &data[i * sizeof(VehicleTarget)];
        payload.targets[i].angle = (int8_t)(p[0] - 0x80);
        payload.targets[i].distance = p[1];
        payload.targets[i].direction = p[2];
        payload.targets[i].speed = p[3];
        payload.targets[i].snr = p[4];
    }

    payload.count = count;
    return payload;
}

static void log_payload(const RadarPayload *payload)
{
    if (payload->count == 0)
    {
        ESP_LOGI(TAG, "No targets");
        return;
    }

    for (int i = 0; i < payload->count; i++)
    {
        const VehicleTarget *t = &payload->targets[i];
        ESP_LOGI(TAG, "Target %d: angle=%d° dist=%dm dir=%s speed=%dkm/h snr=%d",
                 i,
                 t->angle,
                 t->distance,
                 t->direction == 0 ? "away" : "towards",
                 t->speed,
                 t->snr);
    }
}

void radar_task(void *pvParameters)
{
    uint8_t frame_type, sub_type, data[FRAME_MAX_LEN], data_len;

    while (true)
    {
        if (!read_frame(&frame_type, &sub_type, data, &data_len))
        {
            continue;
        }

        RadarPayload payload = parse_targets(data, data_len);
        log_payload(&payload);
        latest_radar_payload = payload;
    }
}
