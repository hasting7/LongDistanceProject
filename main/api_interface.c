#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"

#include "api_interface.h"
#include "net_setup.h"

static const char *TAG = "API";

typedef struct {
    ScreenData *screen;
    const char *url;
} api_get_job_t;

static esp_err_t http_event_handler(esp_http_client_event_t *evt) {
    ScreenData *screen = evt->user_data;

    if (evt->event_id == HTTP_EVENT_ON_DATA &&
        esp_http_client_get_status_code(evt->client) == 200) {
        consume_http_packet(screen, evt->data, evt->data_len);
    }

    return ESP_OK;
}

static void api_get_job(void *pv) {
    api_get_job_t *job = pv;

    esp_http_client_config_t config = {
        .url = job->url,
        .method = HTTP_METHOD_GET,
        .event_handler = http_event_handler,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 15000,
        .user_data = job->screen
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

void api_get(ScreenData *screen, const char *url) {
    api_get_job_t job = {
        .screen = screen,
        .url = url
    };

    net_run_tls_task(api_get_job, &job);
}