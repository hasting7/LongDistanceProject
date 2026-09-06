#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "driver/gpio.h"
#include "esp_sleep.h"
#include "sdkconfig.h"

#include "disk_interface.h"
#include "wifi_interface.h"
#include "api_interface.h"
#include "net_setup.h"
#include "eink_driver.h"
#include "provisioning_interface.h"

#define WIFI_STATUS_PIN (2)
#define PROVISION_BUTTON_GPIO (0)



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
    gpio_set_direction(WIFI_STATUS_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(WIFI_STATUS_PIN, 0);

    while (1) {
        if (wifi_state == WIFI_PENDING) {
            current_state ^= 1;
            gpio_set_level(WIFI_STATUS_PIN, current_state);
        } else if (wifi_state == WIFI_FAILED) {
            gpio_set_level(WIFI_STATUS_PIN, 0);
            break;
        } else if (wifi_state == WIFI_CONNECTED) {
            gpio_set_level(WIFI_STATUS_PIN, 1);
            break;
        }

        vTaskDelay(pdMS_TO_TICKS(750));
    }

    vTaskDelete(NULL);
}

static void provision_button_task(void *pv)
{
    while (gpio_get_level(PROVISION_BUTTON_GPIO) == 0) {
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    while (1) {
        if (gpio_get_level(PROVISION_BUTTON_GPIO) == 0) {
            ESP_LOGI(TAG, "Provision button pressed");
            esp_restart();
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}


void store_screen_to_nvm(const char *endpoint, const char *name) {
    // look at existing id, if exists
    uint8_t current_stored_id = get_screen_id(name);
    uint8_t new_id;

    if (!cloud_check_id(endpoint, &new_id)) {
        ESP_LOGE(TAG, "Failed to check cloud id for \"%s\", skipping update", name);
        return;
    }

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

    uint8_t new_id;
    if (!cloud_check_id(endpoint, &new_id)) {
        ESP_LOGE(TAG, "Failed to check cloud id for %s", endpoint);
        return false;
    }

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

    if (!bitmap) {
        ESP_LOGE(TAG, "Failed to build bitmap for %s, not updating display", endpoint);
    }

    bool ok = bitmap ? display_screen(bitmap) : false;

    if (ok) {
        set_screen_id(CURRENT_SCREEN, new_id);
    }

    delete_screen_data_instance(screen);

    return ok;
}


void enter_deepsleep(const char *reasoning)
{
    int sleep_time = CONFIG_SUCCESS_UPDATE_INTERVAL;

    if (strncmp(reasoning, "Error", 5) == 0) {
        error_screen();
        sleep_time = CONFIG_FAILURE_UPDATE_INTERVAL;
        ESP_LOGE(TAG, "Deep sleeping for %d seconds... %s",
                 sleep_time, reasoning);
    } else {
        ESP_LOGI(TAG, "Deep sleeping for %d seconds... %s",
                 sleep_time, reasoning);
    }

    esp_sleep_enable_timer_wakeup(
        (uint64_t)sleep_time * 1000000ULL
    );

    esp_sleep_enable_ext1_wakeup(
        1ULL << PROVISION_BUTTON_GPIO,
        ESP_EXT1_WAKEUP_ALL_LOW
    );

    esp_deep_sleep_start();
}

void app_main(void)
{
    ESP_LOGI(TAG, "%s running Firmware version: %s", CONFIG_DEVICE_NAME, CONFIG_DEVICE_VERSION);
    
    gpio_set_direction(PROVISION_BUTTON_GPIO, GPIO_MODE_INPUT);
    gpio_pullup_en(PROVISION_BUTTON_GPIO);

    xTaskCreate( provision_button_task,"provision_button",2048,NULL,5,NULL);
    disk_init();
    eink_init();

    esp_reset_reason_t reason = esp_reset_reason();
    esp_sleep_wakeup_cause_t wakeup = esp_sleep_get_wakeup_causes();
    ESP_LOGW(TAG, "WAKEUP = %d, RESET = %d", wakeup, reason);
    if ((wakeup == 8 && reason == 8) || (wakeup == 1 && reason == 3)) {
        // Physical RST / button
        delete_struct(CONFIG_TYPE, "wifi");
        boot_screen();
    } else if (wakeup == 16 && reason == 8) {
        // Deep sleep timer
        // Normal update

    } else if (wakeup == 1 && reason == 1) {
        boot_screen();
    }

    xTaskCreate( wifi_join_state, "wifi_led", 2048, NULL, 5, NULL);

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
            wifi_state = WIFI_FAILED;
            enter_deepsleep("Error: failed to provision wifi");
        }

        size_t size = sizeof(wifi);

        have_wifi = get_struct(CONFIG_TYPE, "wifi", &wifi, &size);
        boot_screen();

        if (!have_wifi) {
            wifi_state = WIFI_FAILED;
            enter_deepsleep("Error: provisioned wifi credentials were lost");
        }
    }

    // connect to wifi
    ESP_LOGI(TAG, "Attempting WiFi connection to \"%s\"", wifi.ssid);

    if (!wifi_join(wifi.ssid, wifi.pwd)) {
        if (wifi_state == WIFI_INVALID) {
            ESP_LOGE(TAG,"Stored Wifi no longer avaliable");
            esp_restart(); // force a reboot
        }
        enter_deepsleep("Error: Failed to connect to Wifi");
    }

    if (!net_setup_wait_ready()) {
        enter_deepsleep("Error: Network never became ready, refusing to make requests");
    }

#ifdef CONFIG_UPDATE_SYSTEM_SCREENS
    ESP_LOGI(TAG,"Force Updating system screens");
    clear_segment(SCREEN_ID_TYPE);
#endif

    bool updated_ok = pull_and_display_latest();

    store_screen_to_nvm("/system/boot", "boot");
    store_screen_to_nvm("/system/error", "error");
    store_screen_to_nvm("/system/qr", "qr");

    if (!updated_ok) {
        enter_deepsleep("Error: Failed to update display");
    } else {
        enter_deepsleep("Update Complete");
    }
}