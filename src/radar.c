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
#define FRAME_HEADER_LEN 4
#define FRAME_MAX_LEN 64

static const char *TAG = "radar";

volatile RadarData latest_radar_data = {0};

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

static bool read_frame(radar_frame_t *out_frame)
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

    // Read type, sub_type, payload_len
    uint8_t header[3];
    int n = uart_read_bytes(RADAR_UART_NUM, header, 3, pdMS_TO_TICKS(100));
    if (n < 3)
    {
        return false;
    }

    uint8_t type = header[0];
    uint8_t sub_type = header[1];
    uint8_t payload_len = header[2]; // remaining bytes including checksum

    if (payload_len == 0 || payload_len > FRAME_MAX_LEN)
    {
        return false;
    }

    uint8_t payload[FRAME_MAX_LEN];
    n = uart_read_bytes(RADAR_UART_NUM, payload, payload_len, pdMS_TO_TICKS(100));
    if (n < payload_len)
    {
        return false;
    }

    uint8_t xor = FRAME_START ^ type ^ sub_type ^ payload_len;
    for (int i = 0; i < payload_len - 1; i++)
    {
        xor ^= payload[i];
    }

    if (xor != payload[payload_len - 1])
    {
        ESP_LOGW(TAG, "Checksum mismatch: expected 0x%02x got 0x%02x", xor, payload[payload_len - 1]);
        return false;
    }

    out_frame->type = type;
    out_frame->sub_type = sub_type;
    out_frame->data_len = payload_len - 1; // exclude checksum
    for (int i = 0; i < out_frame->data_len; i++)
    {
        out_frame->data[i] = payload[i];
    }

    return true;
}

static void log_frame(const radar_frame_t *frame)
{
    ESP_LOGI(TAG, "type=0x%02x sub=0x%02x data(%d):", frame->type, frame->sub_type, frame->data_len);
    ESP_LOG_BUFFER_HEX(TAG, frame->data, frame->data_len);

    if (frame->data_len >= 5)
    {
        int16_t field_a = (int16_t)((frame->data[1] << 8) | frame->data[0]); // bytes 0-1 LE
        int16_t field_b = (int16_t)((frame->data[3] << 8) | frame->data[2]); // bytes 2-3 LE
        int8_t field_c = (int8_t)frame->data[4];                             // byte 4

        ESP_LOGI(TAG, "  field_a=%d field_b=%d field_c=%d", field_a, field_b, field_c);
    }
}

void radar_task(void *pvParameters)
{
    radar_frame_t frame;

    while (true)
    {
        if (read_frame(&frame))
        {
            log_frame(&frame);
            latest_radar_data = frame;
        }
    }
}
