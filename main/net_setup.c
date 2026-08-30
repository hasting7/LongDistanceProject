#include <stdlib.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_netif_sntp.h"
#include "esp_sntp.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"

#include "net_setup.h"
#include "wifi_interface.h"

static const char *TAG = "Net";

// TODO: make this configurable instead of hardcoded once we have a source
// (e.g. IP geolocation or user setting) for the device's real timezone.
#define TIMEZONE_NAME "eastern"
#define TIMEZONE_POSIX_TZ "EST5EDT,M3.2.0,M11.1.0/2"

#define NET_TLS_TASK_STACK 10240
#define NET_TLS_SELFTEST_URL "https://www.google.com/generate_204"

// 2025-01-01T00:00:00Z. If the clock reads earlier than this, SNTP hasn't
// actually landed a believable time yet, whatever esp_netif_sntp_sync_wait()
// says.
#define NET_MIN_BELIEVABLE_TIME ((time_t) 1735689600)

#define NET_SYNC_MAX_ATTEMPTS 4
static const uint32_t kSyncWaitMs[NET_SYNC_MAX_ATTEMPTS] = { 5000, 10000, 20000, 30000 };

static bool s_sntp_started = false;
static volatile bool s_net_ready = false;

typedef struct {
    void (*fn)(void *);
    void *arg;
    SemaphoreHandle_t done;
} net_tls_job_t;

static void net_tls_task(void *pv) {
    net_tls_job_t *job = pv;

    job->fn(job->arg);

    ESP_LOGD(TAG, "TLS task stack high water mark: %u words",
             (unsigned) uxTaskGetStackHighWaterMark(NULL));

    xSemaphoreGive(job->done);
    vTaskDelete(NULL);
}

bool net_run_tls_task(void (*fn)(void *), void *arg) {
    net_tls_job_t job = {
        .fn = fn,
        .arg = arg,
        .done = xSemaphoreCreateBinary(),
    };

    if (!job.done) {
        ESP_LOGE(TAG, "Failed to create TLS task semaphore");
        return false;
    }

    BaseType_t created = xTaskCreate(
        net_tls_task,
        "tls_job",
        NET_TLS_TASK_STACK,
        &job,
        uxTaskPriorityGet(NULL),
        NULL
    );

    if (created != pdPASS) {
        ESP_LOGE(TAG, "Failed to create TLS task");
        vSemaphoreDelete(job.done);
        return false;
    }

    xSemaphoreTake(job.done, portMAX_DELAY);
    vSemaphoreDelete(job.done);
    return true;
}

static bool time_is_believable(void) {
    return time(NULL) >= NET_MIN_BELIEVABLE_TIME;
}

static void log_synced_time(void) {
    time_t now = time(NULL);
    struct tm local_time;
    localtime_r(&now, &local_time);

    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &local_time);
    ESP_LOGI(TAG, "Time synced (%s): %s", TIMEZONE_NAME, buf);
}

bool net_setup_wait_ready(void) {
    if (s_net_ready) {
        return true;
    }

    if (wifi_state != WIFI_CONNECTED) {
        ESP_LOGE(TAG, "WiFi is not connected, refusing to wait on time sync");
        return false;
    }

    setenv("TZ", TIMEZONE_POSIX_TZ, 1);
    tzset();

    if (!s_sntp_started) {
        esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
        esp_err_t err = esp_netif_sntp_init(&config);
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
            ESP_LOGE(TAG, "Failed to start SNTP: %s", esp_err_to_name(err));
            return false;
        }
        s_sntp_started = true;
    }

    for (int attempt = 0; attempt < NET_SYNC_MAX_ATTEMPTS; attempt++) {
        if (attempt > 0) {
            esp_sntp_restart();
        }

        esp_err_t err = esp_netif_sntp_sync_wait(pdMS_TO_TICKS(kSyncWaitMs[attempt]));
        if (err == ESP_OK && time_is_believable()) {
            log_synced_time();
            s_net_ready = true;
            return true;
        }

        ESP_LOGW(TAG, "Time sync attempt %d/%d failed (%s), retrying",
                 attempt + 1, NET_SYNC_MAX_ATTEMPTS, esp_err_to_name(err));
    }

    ESP_LOGE(TAG, "Gave up waiting for time sync, HTTPS requests will not be attempted");
    return false;
}

bool net_is_ready(void) {
    return s_net_ready;
}

const char *net_timezone(void) {
    return s_net_ready ? TIMEZONE_NAME : NULL;
}

static esp_err_t selftest_event_handler(esp_http_client_event_t *evt) {
    (void) evt;
    return ESP_OK;
}

typedef struct {
    const char *url;
} net_selftest_job_t;

static void net_tls_selftest_job(void *pv) {
    net_selftest_job_t *job = pv;

    esp_http_client_config_t config = {
        .url = job->url,
        .method = HTTP_METHOD_GET,
        .event_handler = selftest_event_handler,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 15000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_err_t err = esp_http_client_perform(client);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "TLS selftest status = %d",
                 esp_http_client_get_status_code(client));
    } else {
        ESP_LOGE(TAG, "TLS selftest failed: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
}

bool net_tls_selftest(const char *url) {
    net_selftest_job_t job = {
        .url = url ? url : NET_TLS_SELFTEST_URL,
    };

    return net_run_tls_task(net_tls_selftest_job, &job);
}
