#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"

#include "api_interface.h"

static const char *TAG = "API";

static esp_err_t http_event_handler(esp_http_client_event_t *evt) {
    ScreenData *screen = evt->user_data;

    if (evt->event_id == HTTP_EVENT_ON_DATA) {
        consume_http_packet(screen, evt->data, evt->data_len);
    }

    return ESP_OK;
}

void api_get(ScreenData *screen, const char *url) {
    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .event_handler = http_event_handler,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .user_data = screen
    };

    esp_http_client_handle_t client =
        esp_http_client_init(&config);

    esp_err_t err = esp_http_client_perform(client);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Status = %d",
                 esp_http_client_get_status_code(client));
    } else {
        ESP_LOGE(TAG, "GET failed: %s",
                 esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
}