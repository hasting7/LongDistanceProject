#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"

#include "eink_driver.h"

#define PIN_MOSI    23
#define PIN_SCLK    18
#define PIN_CS       5
#define PIN_DC      17
#define PIN_RST     16
#define PIN_BUSY     4

// ---- Panel geometry (SSD1680, 296x128 physical, RAM packed 128x296) ---
#define EPD_WIDTH_PX   128      // X direction, packed 8 px/byte -> 16 bytes/row
#define EPD_HEIGHT_PX  296      // Y direction, one line per RAM row
#define EPD_WIDTH_BYTES (EPD_WIDTH_PX / 8)          // 16
#define EPD_BUF_SIZE   (EPD_WIDTH_BYTES * EPD_HEIGHT_PX)  // 4736 bytes

#define EPD_BUSY_TIMEOUT_MS 5000

static const char *TAG = "Eink";
static spi_device_handle_t spi;

static void epd_send_cmd(uint8_t cmd) {
    gpio_set_level(PIN_DC, 0);
    spi_transaction_t t = {
        .length = 8,
        .tx_buffer = &cmd,
    };
    esp_err_t ret = spi_device_transmit(spi, &t);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "SPI cmd 0x%02X failed: %s", cmd, esp_err_to_name(ret));
    }
}

static void epd_send_data_byte(uint8_t data) {
    gpio_set_level(PIN_DC, 1); // data
    spi_transaction_t t = {
        .length = 8,
        .tx_buffer = &data,
    };
    esp_err_t ret = spi_device_transmit(spi, &t);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "SPI data 0x%02X failed: %s", data, esp_err_to_name(ret));
    }
}

static void epd_send_data_buf(const uint8_t *data, size_t len) {
    gpio_set_level(PIN_DC, 1); // data
    // SPI transactions are capped in size; chunk large buffers.
    const size_t max_chunk = 4092; // stay under default SPI DMA limit
    size_t offset = 0;
    while (offset < len) {
        size_t chunk = (len - offset > max_chunk) ? max_chunk : (len - offset);
        spi_transaction_t t = {
            .length = chunk * 8,
            .tx_buffer = data + offset,
        };
        esp_err_t ret = spi_device_transmit(spi, &t);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "SPI data_buf chunk at offset %u failed: %s", (unsigned)offset, esp_err_to_name(ret));
        }
        offset += chunk;
    }
}

static bool epd_wait_busy(void) {
    int level = gpio_get_level(PIN_BUSY);
    ESP_LOGI(TAG, "epd_wait_busy: entering, BUSY currently %d", level);
    TickType_t start = xTaskGetTickCount();
    while (gpio_get_level(PIN_BUSY)) {
        if ((xTaskGetTickCount() - start) > pdMS_TO_TICKS(EPD_BUSY_TIMEOUT_MS)) {
            ESP_LOGE(TAG, "epd_wait_busy: TIMED OUT after %d ms, BUSY still %d",
                     EPD_BUSY_TIMEOUT_MS, gpio_get_level(PIN_BUSY));
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    ESP_LOGI(TAG, "epd_wait_busy: cleared after %d ms",
             (int)((xTaskGetTickCount() - start) * portTICK_PERIOD_MS));
    return true;
}

static void epd_reset(void) {
    gpio_set_level(PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(PIN_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(10)); 
    gpio_set_level(PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(20));
    ESP_LOGI(TAG, "epd_reset: RST pulsed, BUSY now %d", gpio_get_level(PIN_BUSY));
}

static void epd_deep_sleep(void) {
    epd_send_cmd(0x10);
    epd_send_data_byte(0x01);
    vTaskDelay(pdMS_TO_TICKS(100));
}

// call once at boot: GPIO + SPI bus setup only
void eink_init(void) {
    // GPIO SETUP
    gpio_config_t out_conf = {
        .pin_bit_mask = (1ULL << PIN_DC) | (1ULL << PIN_RST),
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&out_conf);

    gpio_config_t in_conf = {
        .pin_bit_mask = (1ULL << PIN_BUSY),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
    };
    gpio_config(&in_conf);

    ESP_LOGI(TAG, "eink_init: BUSY pin reads %d before any SPI/reset activity",
             gpio_get_level(PIN_BUSY));

    // SPI SETUP
    spi_bus_config_t buscfg = {
        .mosi_io_num = PIN_MOSI,
        .miso_io_num = -1,
        .sclk_io_num = PIN_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = EPD_BUF_SIZE + 8,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO));

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 4 * 1000 * 1000, // 4 MHz, well under the 20 MHz max
        .mode = 0,
        .spics_io_num = PIN_CS,
        .queue_size = 4,
    };
    ESP_ERROR_CHECK(spi_bus_add_device(SPI2_HOST, &devcfg, &spi));
}


static bool epd_wake_and_configure(void) {
    epd_reset();

    epd_send_cmd(0x12); // SW reset
    if (!epd_wait_busy()) {
        ESP_LOGE(TAG, "Timed out waiting busy after SW reset (0x12) — panel not responding");
        return false;
    }

    epd_send_cmd(0x01); // driver output control: 296 MUX
    epd_send_data_byte(0x27);
    epd_send_data_byte(0x01);
    epd_send_data_byte(0x00);

    epd_send_cmd(0x11); // data entry mode: X increment, Y increment
    epd_send_data_byte(0x03);

    epd_send_cmd(0x44); // set RAM X start/end (byte address, 0..15)
    epd_send_data_byte(0x00);
    epd_send_data_byte(EPD_WIDTH_BYTES - 1);

    epd_send_cmd(0x45); // set RAM Y start/end (0..295)
    epd_send_data_byte(0x00);
    epd_send_data_byte(0x00);
    epd_send_data_byte((EPD_HEIGHT_PX - 1) & 0xFF);
    epd_send_data_byte((EPD_HEIGHT_PX - 1) >> 8);

    epd_send_cmd(0x3C); // border waveform
    epd_send_data_byte(0x05);

    epd_send_cmd(0x21); // display update control 1: normal RAM content
    epd_send_data_byte(0x00);
    epd_send_data_byte(0x80);

    epd_send_cmd(0x18); // read built-in temperature sensor
    epd_send_data_byte(0x80);

    epd_send_cmd(0x4E); // RAM X address counter
    epd_send_data_byte(0x00);
    epd_send_cmd(0x4F); // RAM Y address counter
    epd_send_data_byte(0x00);
    epd_send_data_byte(0x00);

    return true;
}


bool display_screen(uint8_t *frame_ptr) {
    ESP_LOGI(TAG, "display_screen called");
    if (!frame_ptr) {
        ESP_LOGE(TAG, "Screen not complete, cannot display.");
        return false;
    }
    ESP_LOGI(TAG, "Got framebuf, waking display");

    if (!epd_wake_and_configure()) {
        return false;
    }
    ESP_LOGI(TAG, "Display configured, writing RAM");

    epd_send_cmd(0x24);
    epd_send_data_buf(frame_ptr, EPD_BUF_SIZE);
    ESP_LOGI(TAG, "RAM written, triggering update");

    epd_send_cmd(0x22);
    epd_send_data_byte(0xF7);
    epd_send_cmd(0x20);
    if (!epd_wait_busy()) {
        ESP_LOGE(TAG, "Timed out waiting busy after Master Activation (0x20)");
        return false;
    }
    ESP_LOGI(TAG, "Update complete, sleeping");

    epd_deep_sleep();
    return true;
}