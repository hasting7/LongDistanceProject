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

static const char *CURRENT_SCREEN = "cur_id";

uint8_t get_screen_id(const char *name) {
    uint8_t id;
    size_t size = sizeof(id);

    if (get_struct(SCREEN_ID_TYPE, name, &id, &size)) {
        return id;
    }

    return 0;
}

void set_screen_id(const char *name, uint8_t id) {
    store_struct(SCREEN_ID_TYPE, name, &id, sizeof(id));
}


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
    // look at existing id, if exists 
    uint8_t current_stored_id = get_screen_id(name);
    uint8_t new_id = cloud_check_id(endpoint);

    if (current_stored_id == new_id) {
        ESP_LOGI(TAG,"Stored Screen '%s' (id = %d) is up to date, skipping download.", name, new_id);
        return;
    }
    ESP_LOGI(TAG,"Stored Screen '%s' (id = %d -> %d) has been updated, downloading...", name, current_stored_id, new_id);

    ScreenData *screen = create_screen_data_instance(NULL);
    api_get(screen, endpoint);
    uint8_t *screen_data = serve_bitmap(screen, 5000);

    if (!screen_data) {
        ESP_LOGE(TAG, "Screen not ready, not storing \"%s\" to NVM", name);
        delete_screen_data_instance(screen);
        return;
    }

    store_struct(
        SCREEN_TYPE,
        name,
        screen_data,
        EINK_BUFFER_SIZE
    );
    // update stored id
    set_screen_id(name, new_id);
    delete_screen_data_instance(screen);
}


bool show_stored_screen(const char *name) {
    // look at current id, if exists 
    uint8_t current_id = get_screen_id(CURRENT_SCREEN);
    ESP_LOGI(TAG,"Currently displaying screen id %d", current_id);
    uint8_t id = get_screen_id(name);

    if (current_id == id) {
        ESP_LOGI(TAG,"Stored Screen '%s' (id = %d) already being displayed, skipping render.", name, id);
        return true;
    }

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

        set_screen_id(CURRENT_SCREEN, id);

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

bool pull_and_display_latest() {
    char endpoint[64];
    snprintf(endpoint, sizeof(endpoint), "/main/%s", TIMEZONE_NAME);

    uint8_t new_id = cloud_check_id(endpoint);
    uint8_t current_id = get_screen_id(CURRENT_SCREEN);
    ESP_LOGI(TAG, "Currently displaying screen id %d", current_id);

    if (new_id == current_id) {
        ESP_LOGI(TAG, "Screen %s (id = %d) is already being displayed, skipping download.", endpoint, new_id);
        return true;
    }

    ESP_LOGI(TAG, "Screen %s (id = %d) is not being displayed, downloading...", endpoint, new_id);

    ScreenData *screen = create_screen_data_instance(NULL);

    api_get(screen, endpoint);

    uint8_t *bitmap = serve_bitmap(screen, 5000);
    bool ok = bitmap ? display_screen(bitmap) : false;

    set_screen_id(CURRENT_SCREEN, new_id);

    delete_screen_data_instance(screen);

    return ok;
}


void app_main(void)
{
    disk_init();
    eink_init();
    xTaskCreate( wifi_join_state, "wifi_led", 2048, NULL, 5, NULL);

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
        boot_screen();

        if (!have_wifi) {
            ESP_LOGE(TAG,"Provisioning finished but no WiFi credentials were stored");

            wifi_state = WIFI_FAILED;
            error_screen();
            return;
        }
    }

    // connect to wifi
    ESP_LOGI(TAG, "Attempting WiFi connection to \"%s\"", wifi.ssid);

    if (!wifi_join(wifi.ssid, wifi.pwd)) {
        ESP_LOGE(TAG,"Failed to connect to WiFi");
        error_screen();
        return;
    }

    if (!net_setup_wait_ready()) {
        ESP_LOGW(TAG,"Network never became ready, refusing to make requests");
        error_screen();
        return;
    }

#ifdef CONFIG_UPDATE_SYSTEM_SCREENS
    ESP_LOGI(TAG,"Force Updating system screens");
    clear_segment(SCREEN_ID_TYPE);
#endif
    store_screen_to_nvm("/system/boot", "boot");
    store_screen_to_nvm("/system/error", "error");
    store_screen_to_nvm("/system/qr", "qr");


    while (true) {
        pull_and_display_latest();
        vTaskDelay(pdMS_TO_TICKS(5000));
    }    
}