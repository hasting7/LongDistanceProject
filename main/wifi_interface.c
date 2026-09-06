#include <string.h>
#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "sdkconfig.h"

#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "nvs_flash.h"

#include "lwip/err.h"
#include "lwip/sys.h"

#include "wifi_interface.h"

/* FreeRTOS event group to signal when we are connected */
static EventGroupHandle_t s_wifi_event_group;

/*
 * Event bits:
 * - WIFI_CONNECTED_BIT: connected to AP and received an IP
 * - WIFI_FAIL_BIT: failed to connect after maximum retries
 */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static const char *TAG = "wifi station";

static int s_retry_num = 0;

volatile wifi_state_enum wifi_state = WIFI_PENDING;


static void event_handler(
    void* arg,
    esp_event_base_t event_base,
    int32_t event_id,
    void* event_data)
{
    if (event_base == WIFI_EVENT &&
        event_id == WIFI_EVENT_STA_START) {

        ESP_LOGI(TAG, "STA started, attempting to connect...");
        esp_wifi_connect();

    } else if (event_base == WIFI_EVENT &&
               event_id == WIFI_EVENT_STA_DISCONNECTED) {


        wifi_event_sta_disconnected_t *event =
            (wifi_event_sta_disconnected_t *)event_data;

        if (event->reason == WIFI_REASON_AUTH_FAIL) {
            wifi_state = WIFI_INVALID;
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);

        } else if (event->reason == WIFI_REASON_NO_AP_FOUND) {
            wifi_state = WIFI_INVALID;
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        } else {
            ESP_LOGW(TAG,"WiFi disconnected! reason=%d",event->reason);
        }

        if (s_retry_num < CONFIG_WIFI_MAXIMUM_RETRY) {

            esp_err_t err = esp_wifi_connect();

            ESP_LOGI(
                TAG,
                "Retry %d/%d, esp_wifi_connect() = %s",
                s_retry_num + 1,
                CONFIG_WIFI_MAXIMUM_RETRY,
                esp_err_to_name(err)
            );

            s_retry_num++;

        } else {

            ESP_LOGE(
                TAG,
                "Failed to connect after %d retries",
                CONFIG_WIFI_MAXIMUM_RETRY
            );

            xEventGroupSetBits(
                s_wifi_event_group,
                WIFI_FAIL_BIT
            );
        }

    } else if (event_base == IP_EVENT &&
               event_id == IP_EVENT_STA_GOT_IP) {

        ip_event_got_ip_t *event =
            (ip_event_got_ip_t *)event_data;

        ESP_LOGI(
            TAG,
            "got ip:" IPSTR,
            IP2STR(&event->ip_info.ip)
        );

        s_retry_num = 0;

        xEventGroupSetBits(
            s_wifi_event_group,
            WIFI_CONNECTED_BIT
        );
    }
}


void wifi_init(void)
{
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());

    ESP_ERROR_CHECK(
        esp_event_loop_create_default()
    );

    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();

    ESP_ERROR_CHECK(
        esp_wifi_init(&cfg)
    );

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;

    ESP_ERROR_CHECK(
        esp_event_handler_instance_register(
            WIFI_EVENT,
            ESP_EVENT_ANY_ID,
            &event_handler,
            NULL,
            &instance_any_id
        )
    );

    ESP_ERROR_CHECK(
        esp_event_handler_instance_register(
            IP_EVENT,
            IP_EVENT_STA_GOT_IP,
            &event_handler,
            NULL,
            &instance_got_ip
        )
    );

    ESP_ERROR_CHECK(
        esp_wifi_set_mode(WIFI_MODE_STA)
    );

    ESP_LOGI(TAG, "WiFi initialized");
}


bool wifi_join(const char *ssid, const char *password)
{
    wifi_state = WIFI_PENDING;

    /* Reset retry counter for this connection attempt */
    s_retry_num = 0;

    /* Clear any previous result */
    xEventGroupClearBits(
        s_wifi_event_group,
        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT
    );

    /*
     * Configure station.
     *
     * No authentication threshold is specified here so the ESP32
     * can negotiate normally with the access point.
     */
    wifi_config_t wifi_config = {0};

    strlcpy(
        (char *)wifi_config.sta.ssid,
        ssid,
        sizeof(wifi_config.sta.ssid)
    );

    strlcpy(
        (char *)wifi_config.sta.password,
        password,
        sizeof(wifi_config.sta.password)
    );

    ESP_LOGI(TAG, "Configuring SSID: %s", ssid);

    ESP_ERROR_CHECK(
        esp_wifi_set_config(
            WIFI_IF_STA,
            &wifi_config
        )
    );

    ESP_ERROR_CHECK(
        esp_wifi_start()
    );

    ESP_LOGI(
        TAG,
        "Connecting to SSID: %s",
        ssid
    );

    EventBits_t bits = xEventGroupWaitBits(
        s_wifi_event_group,
        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
        pdFALSE,
        pdFALSE,
        portMAX_DELAY
    );

    if (bits & WIFI_CONNECTED_BIT) {

        ESP_LOGI(
            TAG,
            "Connected to AP SSID: %s",
            ssid
        );

        wifi_state = WIFI_CONNECTED;

        esp_netif_ip_info_t ip_info;

        esp_netif_t *netif =
            esp_netif_get_handle_from_ifkey(
                "WIFI_STA_DEF"
            );

        ESP_ERROR_CHECK(
            esp_netif_get_ip_info(
                netif,
                &ip_info
            )
        );

        ESP_LOGI(
            TAG,
            "IP: " IPSTR,
            IP2STR(&ip_info.ip)
        );

        ESP_LOGI(
            TAG,
            "Gateway: " IPSTR,
            IP2STR(&ip_info.gw)
        );

        ESP_LOGI(
            TAG,
            "Netmask: " IPSTR,
            IP2STR(&ip_info.netmask)
        );

        return true;
    }

    if (wifi_state != WIFI_INVALID) {
        wifi_state = WIFI_FAILED;
    }

    if (bits & WIFI_FAIL_BIT) {

        ESP_LOGE(
            TAG,
            "Failed to connect to SSID: %s",
            ssid
        );

        return false;
    }

    ESP_LOGE(TAG, "Unexpected WiFi event");

    return false;
}