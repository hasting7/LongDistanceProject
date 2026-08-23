#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"

#include "bitmap_builder.h"
#include "eink_driver.h"

static const char *TAG = "API";

static esp_err_t http_event_handler(esp_http_client_event_t *evt) {
    switch (evt->event_id) {

        case HTTP_EVENT_ON_DATA:
            printf("Received %d bytes:\n", evt->data_len);
            consume_http_packet(evt->data, evt->data_len);
            break;

        default:
            break;
    }
    // CHANGE
    uint8_t *ptr = serve_bitmap();
    if (ptr) {
        printf("BITMAP WAS READY\n");
        draw_bmp(ptr, 4736);
    } else {
        printf("BITMAP WAS NOT READY\n");
    }
    free(ptr);

    return ESP_OK;
}

void api_get(void)
{
    // CHANGE
    eink_init();

    esp_http_client_config_t config = {
        .url = "http://10.0.0.79/image",
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