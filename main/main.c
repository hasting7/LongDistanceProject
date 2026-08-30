#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "sdkconfig.h"

#include "disk_interface.h"
#include "wifi_interface.h"
#include "gpio_interface.h"
#include "api_interface.h"
#include "net_setup.h"
#include "eink_driver.h"
#include "provisioning_interface.h"

#define WIFI_STATUS_PIN (2)

static const char *TAG = "Main";


void wifi_join_state(void *pvParameters) {
    int current_state = 0;
    gpio_register_output(WIFI_STATUS_PIN);

    while (1) {
        if (wifi_state == WIFI_PENDING) {
            current_state ^= 1;
            gpio_power(WIFI_STATUS_PIN, current_state);
        } else if (wifi_state == WIFI_FAILED) {
            gpio_power(WIFI_STATUS_PIN, false);
            break;
        } else if (wifi_state == WIFI_CONNECTED) {
            gpio_power(WIFI_STATUS_PIN, true);
            break;
        }

        vTaskDelay(pdMS_TO_TICKS(750));
    }

    vTaskDelete(NULL);
}


void store_screen_to_nvm(const char *endpoint, const char *name) {
    ScreenData *screen = create_screen_data_instance();
    api_get(screen, endpoint);

    uint8_t *screen_data = serve_bitmap(screen, 5000);

    if (!screen_data) {
        ESP_LOGE(TAG, "Screen not ready, not storing \"%s\" to NVM", name);
    } else {
        store_struct(
            SCREEN_TYPE,
            name,
            (void *)screen_data,
            EINK_BUFFER_SIZE
        );
    }

    delete_screen_data_instance(screen);
}


bool show_stored_screen(const char *name) {
    uint8_t *screen_buffer = calloc(EINK_BUFFER_SIZE, sizeof(uint8_t));

    if (!screen_buffer) {
        ESP_LOGE(TAG, "Failed to allocate screen buffer");
        return false;
    }

    bool ok = false;
    size_t size = EINK_BUFFER_SIZE;

    if (get_struct(SCREEN_TYPE, name, screen_buffer, &size)) {
        ScreenData *screen =
            create_screen_data_instance_from_mem(screen_buffer, size);

        uint8_t *bitmap = serve_bitmap(screen, 5000);
        ok = bitmap ? display_screen(bitmap) : false;

        delete_screen_data_instance(screen);
    } else {
        ESP_LOGW(TAG, "NO \"%s\" SCREEN", name);
        free(screen_buffer);
    }

    return ok;
}


void boot_screen(void) {
    show_stored_screen("boot");
}


void error_screen(void) {
    show_stored_screen("error");
}


void qr_screen(void) {
    show_stored_screen("qr");
}


bool display(const char *endpoint) {
    ScreenData *screen = create_screen_data_instance();
    api_get(screen, endpoint);

    uint8_t *bitmap = serve_bitmap(screen, 5000);
    bool ok = bitmap ? display_screen(bitmap) : false;

    delete_screen_data_instance(screen);

    return ok;
}


void system_reset(void) {
    clear_segment(SCREEN_TYPE);

    store_screen_to_nvm("/system/boot.bmp", "boot");
    store_screen_to_nvm("/system/error.bmp", "error");
    store_screen_to_nvm("/system/qr.bmp", "qr");
}


void app_main(void)
{
    disk_init();

    xTaskCreate( wifi_join_state, "wifi_led", 2048, NULL, 5, NULL);

    eink_init();
    boot_screen();

    wifi_init();


    // ============================================================
    // Determine WiFi credentials / provisioning mode
    // ====================================================
    WifiDetails wifi;
    bool have_wifi = false;

#if CONFIG_WIFI_BOOT_MODE_FORCE_PROVISIONING

    /*
     * Force provisioning:
     * Always delete stored credentials and enter provisioning mode.
     */
    ESP_LOGW(TAG,"WiFi boot mode: FORCE PROVISIONING");

    ESP_LOGW(TAG,"Clearing any stored WiFi credentials");

    delete_struct(CONFIG_TYPE, "wifi");

#elif CONFIG_WIFI_BOOT_MODE_DEFAULT

    /*
     * Default WiFi:
     * Ignore anything stored in NVS and use the credentials
     * compiled into the firmware through menuconfig.
     */
    ESP_LOGI(TAG,"WiFi boot mode: DEFAULT WIFI");

    strncpy(wifi.pwd,CONFIG_WIFI_DEFAULT_PASSWORD,sizeof(wifi.pwd) - 1);
    strncpy(wifi.ssid,CONFIG_WIFI_DEFAULT_SSID,sizeof(wifi.ssid) - 1);
    wifi.ssid[sizeof(wifi.ssid) - 1] = '\0';
    wifi.pwd[sizeof(wifi.pwd) - 1] = '\0';

    have_wifi = true;

#else

    /*
     * Normal:
     * Try the credentials stored in NVS.
     * If none exist, fall back to provisioning.
     */
    ESP_LOGI(TAG,"WiFi boot mode: NORMAL");

    size_t size = sizeof(wifi);

    have_wifi = get_struct(CONFIG_TYPE,"wifi",&wifi,&size);

#endif


    // ============================================================
    // Provision if credentials are unavailable
    // ============================================================

    if (!have_wifi) {

        ESP_LOGW(TAG,"No stored WiFi credentials, entering provisioning mode");

        qr_screen();

        if (!provisioning_start()) {
            ESP_LOGE(TAG,"Failed to start provisioning");

            wifi_state = WIFI_FAILED;
            error_screen();
            return;
        }

        size_t size = sizeof(wifi);

        have_wifi = get_struct(CONFIG_TYPE, "wifi", &wifi, &size);

        if (!have_wifi) {
            ESP_LOGE(TAG,"Provisioning finished but no WiFi credentials were stored");

            wifi_state = WIFI_FAILED;
            error_screen();
            return;
        }
    }

    // connect to wifi
    boot_screen();
    ESP_LOGI(TAG, "Attempting WiFi connection to \"%s\"", wifi.ssid);

    if (!wifi_join(wifi.ssid, wifi.pwd)) {
        ESP_LOGE(TAG,"Failed to connect to WiFi");
        error_screen();
        return;
    }

    if (!net_setup_wait_ready()) {
        ESP_LOGE(TAG,"Network never became ready, refusing to make requests");
        error_screen();
        return;
    }
#ifdef CONFIG_UPDATE_SYSTEM_SCREENS
    ESP_LOGI(TAG,"Updating system screens");
    system_reset();
#endif

    display("/main/pacific.bmp");
}