#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"

static const char *TAG = "API";

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    switch (evt->event_id) {

        case HTTP_EVENT_ON_DATA:
            printf("Received %d bytes:\n", evt->data_len);
            printf("%.*s\n", evt->data_len, (char *)evt->data);
            break;

        default:
            break;
    }

    return ESP_OK;
}

void api_get(void)
{
    esp_http_client_config_t config = {
        .url = "http://jsonplaceholder.typicode.com/todos/1",
        .method = HTTP_METHOD_GET,
        .event_handler = http_event_handler,
        .crt_bundle_attach = esp_crt_bundle_attach,
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