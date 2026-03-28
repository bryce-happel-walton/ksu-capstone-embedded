#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "led_strip.h"
#include "driver/gpio.h"

#include "shared_lib.h"
#include "pindefs.h"
#include "led.h"

volatile TestDisplayPattern current_display_pattern = DISPLAY_PATTERN_CORNERS;

led_strip_handle_t configure_led(void)
{
    led_strip_config_t strip_config = {
        .strip_gpio_num = LED_STRIP_GPIO_PIN,
        .max_leds = PIXELS,
        .led_model = LED_MODEL_WS2812,
        .color_component_format = {
            .format = {
                .r_pos = 1,
                .g_pos = 0,
                .b_pos = 2,
                .num_components = 3,
            },
        },
        .flags = {
            .invert_out = false,
        }};

    led_strip_spi_config_t spi_config = {
        .clk_src = SPI_CLK_SRC_DEFAULT,
        .spi_bus = SPI2_HOST,
        .flags = {
            .with_dma = true,
        }};

    led_strip_handle_t led_strip;
    ESP_ERROR_CHECK(led_strip_new_spi_device(&strip_config, &spi_config, &led_strip));
    return led_strip;
}

int led_index(int row, int col)
{
    int panel_row = row / LED_PANEL_ROWS;
    int panel_col = col / LED_PANEL_COLS;

    int local_row = row % LED_PANEL_ROWS;
    int local_col = col % LED_PANEL_COLS;

    int chain_pos;
    bool flip_row, flip_col;
    if (panel_row == 1 && panel_col == 1)
    {
        chain_pos = 0;
        flip_row = true;
        flip_col = false; // bottom-right
    }
    else if (panel_row == 0 && panel_col == 1)
    {
        chain_pos = 1;
        flip_row = true;
        flip_col = true; // top-right
    }
    else if (panel_row == 0 && panel_col == 0)
    {
        chain_pos = 2;
        flip_row = false;
        flip_col = true; // top-left
    }
    else
    {
        chain_pos = 3;
        flip_row = false;
        flip_col = false; // bottom-left
    }

    int effective_row = flip_row ? (LED_PANEL_ROWS - 1 - local_row) : local_row;
    int effective_col = flip_col ? (LED_PANEL_COLS - 1 - local_col) : local_col;

    // Serpentine: account for panel orientation when determining reversal direction
    if ((effective_row + flip_row + flip_col) % 2 == 0)
    {
        effective_col = LED_PANEL_COLS - 1 - effective_col;
    }

    return chain_pos * (LED_PANEL_ROWS * LED_PANEL_COLS) + effective_row * LED_PANEL_COLS + effective_col;
}

static void led_pattern_corners(led_strip_handle_t led_strip, int *step)
{
    int max_row = LED_PANEL_ROWS * LED_PANELS_HIGH - 1;
    int max_col = LED_PANEL_COLS * LED_PANELS_WIDE - 1;
    int corners[][2] = {
        {0, 0},
        {0, max_col},
        {max_row, max_col},
        {max_row, 0},
    };

    led_strip_clear(led_strip);
    vTaskDelay(pdMS_TO_TICKS(MIN_LED_RESET_TIME_MS));
    led_strip_set_pixel(led_strip, led_index(corners[*step][0], corners[*step][1]), 10, 0, 0);
    led_strip_refresh(led_strip);
    *step = (*step + 1) % 4;
    vTaskDelay(pdMS_TO_TICKS(300));
}

static void led_pattern_centers(led_strip_handle_t led_strip, int *step)
{
    int center_row = (LED_PANEL_ROWS * LED_PANELS_HIGH) / 2;
    int center_col = (LED_PANEL_COLS * LED_PANELS_WIDE) / 2;
    int centers[][2] = {
        {center_row - 1, center_col - 1}, // center of top-left panel
        {center_row - 1, center_col},     // center of top-right panel
        {center_row, center_col},         // center of bottom-right panel
        {center_row, center_col - 1},     // center of bottom-left panel
    };

    led_strip_clear(led_strip);
    vTaskDelay(pdMS_TO_TICKS(MIN_LED_RESET_TIME_MS));
    led_strip_set_pixel(led_strip, led_index(centers[*step][0], centers[*step][1]), 0, 10, 0);
    led_strip_refresh(led_strip);
    *step = (*step + 1) % 4;
    vTaskDelay(pdMS_TO_TICKS(300));
}

static void led_pattern_encircle(led_strip_handle_t led_strip, int *step)
{
    int total_rows = LED_PANEL_ROWS * LED_PANELS_HIGH;
    int total_cols = LED_PANEL_COLS * LED_PANELS_WIDE;
    int perimeter = 2 * (total_rows + total_cols) - 4;

    int row, col;
    int s = *step % perimeter;

    if (s < total_cols)
    {
        row = 0;
        col = s;
    }
    else if (s < total_cols + total_rows - 1)
    {
        row = s - total_cols + 1;
        col = total_cols - 1;
    }
    else if (s < 2 * total_cols + total_rows - 2)
    {
        row = total_rows - 1;
        col = total_cols - 1 - (s - total_cols - total_rows + 2);
    }
    else
    {
        row = total_rows - 1 - (s - 2 * total_cols - total_rows + 3);
        col = 0;
    }

    led_strip_clear(led_strip);
    vTaskDelay(pdMS_TO_TICKS(MIN_LED_RESET_TIME_MS));
    led_strip_set_pixel(led_strip, led_index(row, col), 0, 0, 10);
    led_strip_refresh(led_strip);
    *step = (*step + 1) % perimeter;
    vTaskDelay(pdMS_TO_TICKS(25));
}

static void led_pattern_shrink_encircle(led_strip_handle_t led_strip, int *step)
{
    int total_rows = LED_PANEL_ROWS * LED_PANELS_HIGH;
    int total_cols = LED_PANEL_COLS * LED_PANELS_WIDE;
    int max_layers = (total_rows < total_cols ? total_rows : total_cols) / 2;

    int pos = *step;
    int layer = 0;
    while (layer < max_layers)
    {
        int rows_in_layer = total_rows - 2 * layer;
        int cols_in_layer = total_cols - 2 * layer;
        if ((rows_in_layer <= 0) || (cols_in_layer <= 0))
            break;
        int perimeter = 2 * (rows_in_layer + cols_in_layer) - 4;
        if (pos < perimeter)
            break;
        pos -= perimeter;
        layer++;
    }

    if (layer >= max_layers)
    {
        *step = 0;
        return;
    }

    int top = layer;
    int left = layer;
    int bottom = total_rows - 1 - layer;
    int right = total_cols - 1 - layer;
    int rows_in_layer = bottom - top + 1;
    int cols_in_layer = right - left + 1;

    int row, col;
    if (pos < cols_in_layer)
    {
        row = top;
        col = left + pos;
    }
    else if (pos < cols_in_layer + rows_in_layer - 1)
    {
        row = top + (pos - cols_in_layer + 1);
        col = right;
    }
    else if (pos < 2 * cols_in_layer + rows_in_layer - 2)
    {
        row = bottom;
        col = right - (pos - cols_in_layer - rows_in_layer + 2);
    }
    else
    {
        row = bottom - (pos - 2 * cols_in_layer - rows_in_layer + 3);
        col = left;
    }

    led_strip_clear(led_strip);
    vTaskDelay(pdMS_TO_TICKS(MIN_LED_RESET_TIME_MS));
    led_strip_set_pixel(led_strip, led_index(row, col), 0, 0, 10);
    led_strip_refresh(led_strip);
    (*step)++;
    vTaskDelay(pdMS_TO_TICKS(10));
}

static void led_pattern_shrink_square(led_strip_handle_t led_strip, int *step)
{
    int total_rows = LED_PANEL_ROWS * LED_PANELS_HIGH;
    int total_cols = LED_PANEL_COLS * LED_PANELS_WIDE;
    int max_layers = (total_rows < total_cols ? total_rows : total_cols) / 2;
    int layer = *step % max_layers;

    int top = layer;
    int left = layer;
    int bottom = total_rows - 1 - layer;
    int right = total_cols - 1 - layer;

    led_strip_clear(led_strip);
    vTaskDelay(pdMS_TO_TICKS(MIN_LED_RESET_TIME_MS));

    for (int col = left; col <= right; col++)
    {
        led_strip_set_pixel(led_strip, led_index(top, col), 255, 0, 10);
        led_strip_set_pixel(led_strip, led_index(bottom, col), 255, 0, 10);
    }
    for (int row = top + 1; row < bottom; row++)
    {
        led_strip_set_pixel(led_strip, led_index(row, left), 255, 0, 10);
        led_strip_set_pixel(led_strip, led_index(row, right), 255, 0, 10);
    }

    led_strip_refresh(led_strip);
    *step = (*step + 1) % max_layers;
    vTaskDelay(pdMS_TO_TICKS(300));
}

void led_task(void *pvParameters)
{
    led_strip_handle_t led_strip = (led_strip_handle_t)pvParameters;

    int step = 0;
    TestDisplayPattern last_pattern = current_display_pattern;

    while (true)
    {
        if (current_display_pattern != last_pattern)
        {
            step = 0;
            last_pattern = current_display_pattern;
        }

        switch (current_display_pattern)
        {
        case DISPLAY_PATTERN_CORNERS:
            led_pattern_corners(led_strip, &step);
            break;
        case DISPLAY_PATTERN_CENTERS:
            led_pattern_centers(led_strip, &step);
            break;
        case DISPLAY_PATTERN_ENCIRCLE:
            led_pattern_encircle(led_strip, &step);
            break;
        case DISPLAY_PATTERN_SHRINK_ENCIRCLE:
            led_pattern_shrink_encircle(led_strip, &step);
            break;
        case DISPLAY_PATTERN_SHRINK_SQUARE:
            led_pattern_shrink_square(led_strip, &step);
            break;
        }
    }
}
