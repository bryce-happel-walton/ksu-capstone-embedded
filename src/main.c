#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_mac.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "nvs_flash.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "esp_camera.h"
#include "led_strip.h"
#include "driver/gpio.h"

#include "shared_lib.h"
#include "pindefs.h"

#define MAX_STA_CONN 4

static const char *TAG = "Capstone Speed Sign";
static httpd_handle_t data_server = NULL;
static httpd_handle_t stream_server = NULL;

static camera_config_t camera_config = {
    .pin_pwdn = CAM_PIN_PWDN,
    .pin_reset = CAM_PIN_RESET,
    .pin_xclk = CAM_PIN_XCLK,
    .pin_sccb_sda = CAM_PIN_SIOD,
    .pin_sccb_scl = CAM_PIN_SIOC,

    .pin_d7 = CAM_PIN_D7,
    .pin_d6 = CAM_PIN_D6,
    .pin_d5 = CAM_PIN_D5,
    .pin_d4 = CAM_PIN_D4,
    .pin_d3 = CAM_PIN_D3,
    .pin_d2 = CAM_PIN_D2,
    .pin_d1 = CAM_PIN_D1,
    .pin_d0 = CAM_PIN_D0,
    .pin_vsync = CAM_PIN_VSYNC,
    .pin_href = CAM_PIN_HREF,
    .pin_pclk = CAM_PIN_PCLK,

    .ledc_timer = LEDC_TIMER_0,
    .ledc_channel = LEDC_CHANNEL_0,

    .pixel_format = PIXFORMAT_JPEG,

#if CONFIG_IDF_TARGET_ESP32S3
    .xclk_freq_hz = 20000000,

    .frame_size = FRAMESIZE_HD,
    .jpeg_quality = 6,
    .fb_count = 2,
    .fb_location = CAMERA_FB_IN_PSRAM,
    .grab_mode = CAMERA_GRAB_LATEST,
#else
    .xclk_freq_hz = 20000000,

    .frame_size = FRAMESIZE_VGA, // Smaller frame for DRAM
    .jpeg_quality = 8,           // lowest compression possible on esp32-cam
    .fb_count = 1,
    .fb_location = CAMERA_FB_IN_DRAM,
    .grab_mode = CAMERA_GRAB_WHEN_EMPTY,
#endif

};

led_strip_handle_t configure_led(void)
{
    // LED strip general initialization, according to your led board design
    led_strip_config_t strip_config = {
        .strip_gpio_num = LED_STRIP_GPIO_PIN,
        .max_leds = LED_COUNT,
        .led_model = LED_MODEL_WS2812,
        // set the color order of the strip: GRB
        .color_component_format = {
            .format = {
                .r_pos = 1,          // red is the second byte in the color data
                .g_pos = 0,          // green is the first byte in the color data
                .b_pos = 2,          // blue is the third byte in the color data
                .num_components = 3, // total 3 color components
            },
        },
        .flags = {
            .invert_out = false, // don't invert the output signal
        }};

    // LED strip backend configuration: SPI
    led_strip_spi_config_t spi_config = {
        .clk_src = SPI_CLK_SRC_DEFAULT,
        .spi_bus = SPI2_HOST,
        .flags = {
            .with_dma = true,
        }};

    // LED Strip object handle
    led_strip_handle_t led_strip;
    ESP_ERROR_CHECK(led_strip_new_spi_device(&strip_config, &spi_config, &led_strip));
    ESP_LOGI(TAG, "Created LED strip object with SPI backend");
    return led_strip;
}

esp_err_t init_camera(void)
{
    esp_err_t ret = esp_camera_init(&camera_config);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Camera init failed: 0x%x", ret);
    }

    return ret;
}

void wifi_event_handler(void *arg, esp_event_base_t event_base,
                        int32_t event_id, void *event_data)
{
    switch (event_id)
    {
    case WIFI_EVENT_AP_STACONNECTED:
    {
        wifi_event_ap_staconnected_t *connected_event = (wifi_event_ap_staconnected_t *)event_data;
        ESP_LOGI(TAG, "station " MACSTR " join, AID=%d", MAC2STR(connected_event->mac), connected_event->aid);
        break;
    }

    case WIFI_EVENT_AP_STADISCONNECTED:
    {
        wifi_event_ap_stadisconnected_t *disconnected_event = (wifi_event_ap_stadisconnected_t *)event_data;
        ESP_LOGI(TAG, "station " MACSTR " leave, AID=%d", MAC2STR(disconnected_event->mac), disconnected_event->aid);
        break;
    }

    default:
        break;
    }
}

esp_err_t ws_test_data_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET)
    {
        ESP_LOGI(TAG, "Handshake done, test_data connection opened");
        return ESP_OK;
    }

    return ESP_FAIL;
}

esp_err_t ws_image_stream_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET)
    {
        ESP_LOGI(TAG, "Handshake done, image stream connection opened");
        return ESP_OK;
    }

    return ESP_FAIL;
}

void websocket_test_data_task(void *pvParameters)
{
    static uint32_t inc_num = 0;
    static bool inc_bool = false;

    while (true)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));

        size_t clients = 10;
        int fds[10];
        if (httpd_get_client_list(data_server, &clients, fds) == ESP_OK)
        {
            for (int i = 0; i < clients; i++)
            {
                if (httpd_ws_get_fd_info(data_server, fds[i]) == HTTPD_WS_CLIENT_WEBSOCKET)
                {
                    TestData test = {
                        .hello = "Data from ESP!",
                        .beep = inc_num,
                        .boop = inc_bool,
                    };

                    httpd_ws_frame_t frame = {
                        .type = HTTPD_WS_TYPE_BINARY,
                        .final = true,
                        .payload = (uint8_t *)&test,
                        .len = sizeof(TestData),
                    };

                    esp_err_t ret = httpd_ws_send_frame_async(data_server, fds[i], &frame);
                    if (ret != ESP_OK)
                    {
                        ESP_LOGW(TAG, "Failed to send test data to fd %d: %s", fds[i], esp_err_to_name(ret));
                        httpd_sess_trigger_close(data_server, fds[i]);
                    }
                    else
                    {
                        inc_num++;
                        inc_bool = !inc_bool;
                    }
                }
            }
        }
    }
}

void image_stream_task(void *pvParameters)
{
    while (true)
    {
        camera_fb_t *fb = esp_camera_fb_get();
        if (!fb)
        {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        size_t clients = 10;
        int fds[10];
        if (httpd_get_client_list(stream_server, &clients, fds) == ESP_OK)

        {
            for (int i = 0; i < clients; i++)
            {
                if (httpd_ws_get_fd_info(stream_server, fds[i]) == HTTPD_WS_CLIENT_WEBSOCKET)
                {
                    httpd_ws_frame_t frame = {
                        .type = HTTPD_WS_TYPE_BINARY,
                        .payload = fb->buf,
                        .len = fb->len,
                    };

                    esp_err_t ret = httpd_ws_send_frame_async(stream_server, fds[i], &frame);
                    if (ret != ESP_OK)
                    {
                        ESP_LOGW(TAG, "Stream send failed fd %d: %s", fds[i], esp_err_to_name(ret));
                        httpd_sess_trigger_close(stream_server, fds[i]);
                    }
                }
            }
        }

        esp_camera_fb_return(fb);

        vTaskDelay(pdMS_TO_TICKS(1000 / 30));
    }
}

uint32_t led_index(uint32_t row, uint32_t col)
{
}

void led_task(void *pvParameters)
{
    led_strip_handle_t led_strip = (led_strip_handle_t)pvParameters;

    while (true)
    {
        for (int i = 0; i < PIXELS; i++)
        {
            led_strip_clear(led_strip);
            vTaskDelay(pdMS_TO_TICKS(50));
            led_strip_set_pixel(led_strip, i, 255, 0, 0);
            led_strip_refresh(led_strip);
            vTaskDelay(pdMS_TO_TICKS(300));
        }
    }
}

void start_webserver(void)
{
    httpd_config_t data_config = HTTPD_DEFAULT_CONFIG();
    data_config.server_port = TEST_DATA_PORT;
    data_config.ctrl_port = 32768;

    httpd_uri_t test_data_get_uri = {
        .uri = "/" TEST_DATA_URI,
        .method = HTTP_GET,
        .handler = ws_test_data_handler,
        .user_ctx = NULL,
        .is_websocket = true,
    };

    if (httpd_start(&data_server, &data_config) == ESP_OK)
    {
        httpd_register_uri_handler(data_server, &test_data_get_uri);
        xTaskCreate(websocket_test_data_task, "test_data_task", 4096, NULL, 5, NULL);
    }

    httpd_config_t stream_config = HTTPD_DEFAULT_CONFIG();
    stream_config.server_port = IMAGE_STREAM_PORT;
    stream_config.ctrl_port = 32769;

    httpd_uri_t ws_stream_uri = {
        .uri = "/" IMAGE_STREAM_URI,
        .method = HTTP_GET,
        .handler = ws_image_stream_handler,
        .is_websocket = true,
    };

    if (httpd_start(&stream_server, &stream_config) == ESP_OK)
    {
        httpd_register_uri_handler(stream_server, &ws_stream_uri);
        xTaskCreate(image_stream_task, "image_stream", 8192, NULL, 5, NULL);
    }
}

void wifi_init_softap(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
            .ssid_len = strlen(WIFI_SSID),
            .channel = WIFI_CHANNEL,
            .authmode = strlen(WIFI_PASS) == 0 ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA_WPA2_PSK,
            .max_connection = MAX_STA_CONN,
            .pmf_cfg = {
                .required = false,
            },
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "wifi_init_softap finished. SSID:%s channel:%d", WIFI_SSID, WIFI_CHANNEL);
}

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    vTaskDelay(pdMS_TO_TICKS(3000));

    esp_err_t cam_ret = init_camera();
    led_strip_handle_t led_strip = configure_led();

    ESP_LOGI(TAG, "ESP_WIFI_MODE_AP");
    wifi_init_softap();
    start_webserver();

    ESP_ERROR_CHECK(cam_ret);
    xTaskCreate(led_task, "led_task", 4096, (void *)led_strip, 5, NULL);
}
