#include <string.h>
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

#include "shared_lib.h"

// ESP32-CAM pin definitions
#define CAM_PIN_PWDN 32
#define CAM_PIN_RESET -1
#define CAM_PIN_XCLK 0
#define CAM_PIN_SIOD 26
#define CAM_PIN_SIOC 27
#define CAM_PIN_D7 35
#define CAM_PIN_D6 34
#define CAM_PIN_D5 39
#define CAM_PIN_D4 36
#define CAM_PIN_D3 21
#define CAM_PIN_D2 19
#define CAM_PIN_D1 18
#define CAM_PIN_D0 5
#define CAM_PIN_VSYNC 25
#define CAM_PIN_HREF 23
#define CAM_PIN_PCLK 22

#define MAX_STA_CONN 4

static const char *TAG = "wifi softAP";
static httpd_handle_t server = NULL;

static esp_err_t camera_init(void)
{
    camera_config_t config = {
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

        .xclk_freq_hz = 20000000,
        .ledc_timer = LEDC_TIMER_0,
        .ledc_channel = LEDC_CHANNEL_0,
        .pixel_format = PIX_FORMAT,
        .frame_size = FRAMESIZE_VGA,
        .jpeg_quality = 12,
        .fb_count = 2,
        .fb_location = CAMERA_FB_IN_PSRAM,
        .grab_mode = CAMERA_GRAB_WHEN_EMPTY,
    };

    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Camera init failed: 0x%x", err);
    }
    return err;
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
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

static esp_err_t ws_handler(httpd_req_t *req)
{
    return ESP_OK;
}

void webserver_test_task(void *pvParameters)
{
    static uint32_t inc_num = 0;
    static bool inc_bool = false;

    while (true)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));

        size_t clients = 10;
        int fds[10];
        if (httpd_get_client_list(server, &clients, fds) == ESP_OK)
        {
            for (int i = 0; i < clients; i++)
            {
                if (httpd_ws_get_fd_info(server, fds[i]) == HTTPD_WS_CLIENT_WEBSOCKET)
                {
                    inc_bool = !inc_bool;
                    TestData test = {
                        .hello = "World! from ESP",
                        .beep = inc_num++,
                        .boop = inc_bool,
                    };

                    httpd_ws_frame_t frame = {
                        .type = HTTPD_WS_TYPE_BINARY,
                        .payload = (uint8_t *)&test,
                        .len = sizeof(TestData),
                    };

                    esp_err_t ret = httpd_ws_send_frame_async(server, fds[i], &frame);
                    if (ret != ESP_OK)
                        ESP_LOGW(TAG, "Failed to send test data to fd %d: %s", fds[i], esp_err_to_name(ret));
                }
            }
        }
    }
}

static void camera_stream_task(void *pvParameters)
{
    while (true)
    {
        camera_fb_t *fb = esp_camera_fb_get();
        if (!fb)
        {
            ESP_LOGE(TAG, "Failed to capture frame");
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        size_t clients = 10;
        int fds[10];
        if (httpd_get_client_list(server, &clients, fds) == ESP_OK)
        {
            for (int i = 0; i < clients; i++)
            {
                if (httpd_ws_get_fd_info(server, fds[i]) == HTTPD_WS_CLIENT_WEBSOCKET)
                {
                    httpd_ws_frame_t frame = {
                        .type = HTTPD_WS_TYPE_BINARY,
                        .payload = fb->buf,
                        .len = fb->len,
                    };

                    esp_err_t ret = httpd_ws_send_frame_async(server, fds[i], &frame);
                    if (ret != ESP_OK)
                        ESP_LOGW(TAG, "Stream send failed fd %d: %s", fds[i], esp_err_to_name(ret));
                }
            }
        }

        esp_camera_fb_return(fb);
        vTaskDelay(pdMS_TO_TICKS(33)); // ~30 fps target
    }
}

void start_webserver(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.enable_so_linger = false;
    config.max_uri_handlers = 4;

    httpd_uri_t ws_data_uri = {
        .uri = "/data",
        .method = HTTP_GET,
        .handler = ws_handler,
        .is_websocket = true,
    };

    httpd_uri_t ws_stream_uri = {
        .uri = "/stream",
        .method = HTTP_GET,
        .handler = ws_handler,
        .is_websocket = true,
    };

    if (httpd_start(&server, &config) == ESP_OK)
    {
        httpd_register_uri_handler(server, &ws_data_uri);
        httpd_register_uri_handler(server, &ws_stream_uri);
        xTaskCreate(webserver_test_task, "send_task", 4096, NULL, 5, NULL);
        xTaskCreate(camera_stream_task, "cam_stream", 4096, NULL, 5, NULL);
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

    ESP_ERROR_CHECK(camera_init());

    ESP_LOGI(TAG, "ESP_WIFI_MODE_AP");
    wifi_init_softap();
    start_webserver();
}
