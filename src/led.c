#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "led_strip.h"
#include "driver/gpio.h"

#include "shared_lib.h"
#include "pindefs.h"
#include "led.h"
#include "font.h"

volatile TestDisplayPattern current_display_pattern = DISPLAY_PATTERN_SLOW_DOWN;

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

static void led_pattern_font_test(led_strip_handle_t led_strip, int *step)
{
    static const int GLYPH_W = 5;
    static const int GLYPH_H = 7;
    static const int COL_STEP = 6; // glyph width + 1px gap
    static const int ROW_STEP = 8; // glyph height + 1px gap
    static const int COLS = 5;
    static const int ROWS = 4;
    static const int TOTAL = sizeof(font5x7) / sizeof(font5x7[0]);

    static const int COL_ORIGIN = 1;
    static const int ROW_ORIGIN = 0;

    for (int ti = 0; ti < ROWS * COLS; ti++)
    {
        int char_idx = (*step + ti) % TOTAL;
        char *bitmap = font5x7[char_idx];
        int base_row = ROW_ORIGIN + (ti / COLS) * ROW_STEP;
        int base_col = COL_ORIGIN + (ti % COLS) * COL_STEP;

        for (int r = 0; r < GLYPH_H; r++)
            for (int c = 0; c < GLYPH_W; c++)
                if (bitmap[r] & (1 << c))
                    led_strip_set_pixel(led_strip, led_index(base_row + r, base_col + c), 10, 0, 0);
    }

    led_strip_refresh(led_strip);
    *step = (*step + 1) % TOTAL;
    vTaskDelay(pdMS_TO_TICKS(200));
}

static void led_pattern_number_font_test(led_strip_handle_t led_strip, int *step)
{
    int left_digit = *step % 10;
    int right_digit = (9 - *step + 10) % 10;

    int num_rows = sizeof(font_num[0]) / sizeof(font_num[0][0]); // 32
    int num_cols = LED_PANEL_COLS;                               // 16

    for (int row = 0; row < num_rows; row++)
    {
        short left_row = font_num[left_digit][row];
        short right_row = font_num[right_digit][row];

        for (int col = 0; col < num_cols; col++)
        {
            if (left_row & (1 << col))
                led_strip_set_pixel(led_strip, led_index(row, col), 10, 0, 0);
            if (right_row & (1 << col))
                led_strip_set_pixel(led_strip, led_index(row, col + num_cols), 0, 10, 0);
        }
    }

    led_strip_refresh(led_strip);
    *step = (*step + 1) % 10;
    vTaskDelay(pdMS_TO_TICKS(500));
}

static void led_pattern_slow_down(led_strip_handle_t led_strip, int *step)
{
    const char *line1 = "SLOW";
    const char *line2 = "DOWN";
    const int start_row = 8; // (32 - 16) / 2

    for (int bitmap_index = 0; bitmap_index < 4; bitmap_index++)
    {
        char *bitmap_1 = font8x8_basic[(unsigned char)line1[bitmap_index]];
        char *bitmap_2 = font8x8_basic[(unsigned char)line2[bitmap_index]];
        int col_base = bitmap_index * 8;

        for (int r = 0; r < 8; r++)
        {
            for (int c = 0; c < 8; c++)
            {
                if (bitmap_1[r] & (1 << c))
                    led_strip_set_pixel(led_strip, led_index(start_row + r, col_base + c), 10, 4, 0);
                if (bitmap_2[r] & (1 << c))
                    led_strip_set_pixel(led_strip, led_index(start_row + 8 + r, col_base + c), 10, 4, 0);
            }
        }
    }

    led_strip_refresh(led_strip);
    vTaskDelay(pdMS_TO_TICKS(500));
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

        led_strip_clear(led_strip);
        vTaskDelay(pdMS_TO_TICKS(MIN_LED_RESET_TIME_MS));

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
        case DISPLAY_PATTERN_FONT_TEST:
            led_pattern_font_test(led_strip, &step);
            break;
        case DISPLAY_PATTERN_FONT_NUM_SEGMENT_TEST:
            led_pattern_number_font_test(led_strip, &step);
            break;
        case DISPLAY_PATTERN_SLOW_DOWN:
            led_pattern_slow_down(led_strip, &step);
            break;
        }
    }
}
