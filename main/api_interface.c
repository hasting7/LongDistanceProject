#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include <time.h>

#include "api_interface.h"
#include "net_setup.h"


static const char *TAG = "API";

typedef struct {
    ScreenData *screen;
    char url[256];
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

void api_get(ScreenData *screen, const char *path) {
    api_get_job_t job = {
        .screen = screen
    };

    snprintf(
        job.url,
        sizeof(job.url),
        "%s%s.bmp?v=%lu",
        SERVER_NAME,
        path,
        (unsigned long)time(NULL)
    );
    ESP_LOGI(TAG, "Requesting = %s", job.url);

    net_run_tls_task(api_get_job, &job);
}

static esp_err_t id_event_handler(esp_http_client_event_t *evt) {
    uint8_t *id = evt->user_data;

    if (evt->event_id == HTTP_EVENT_ON_DATA && evt->data_len > 0) {
        char buffer[4] = {0};
        int len = evt->data_len < 3 ? evt->data_len : 3;

        memcpy(buffer, evt->data, len);

        int value = atoi(buffer);
        if (value >= 0 && value <= 255) {
            *id = (uint8_t)value;
        }
    }

    return ESP_OK;
}

uint8_t cloud_check_id(const char *endpoint) {
    uint8_t id = 0;
    char url[256];

    snprintf(
        url,
        sizeof(url),
        "%s%s.id?v=%lu",
        SERVER_NAME,
        endpoint,
        (unsigned long)time(NULL)
    );

    ESP_LOGI(TAG, "Fetching screen ID: %s", url);

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .event_handler = id_event_handler,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 3000,
        .user_data = &id
    };

    esp_http_client_handle_t client =
        esp_http_client_init(&config);

    if (client == NULL) {
        ESP_LOGE(TAG, "Failed to initialize HTTP client");
        return 0;
    }

    esp_err_t err = esp_http_client_perform(client);

    if (err != ESP_OK ||
        esp_http_client_get_status_code(client) != 200) {
        ESP_LOGE(TAG, "Failed to fetch screen ID: %s", esp_err_to_name(err));
        id = 0;
    } else {
        ESP_LOGI(TAG, "Screen ID = %u", id);
    }

    esp_http_client_cleanup(client);

    return id;
}