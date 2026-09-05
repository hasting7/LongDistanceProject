#include "disk_interface.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_image_format.h"
#include <stdio.h>

const char* CONFIG_TYPE = "config";
const char* USER_TYPE = "user";
const char* SCREEN_TYPE = "screen";
const char* SCREEN_ID_TYPE = "screenid";


void store_struct(const char *type, const char *recall_name, void *data_in, size_t struct_size) {
	nvs_handle_t handle;
	ESP_ERROR_CHECK(nvs_open(type, NVS_READWRITE, &handle));
	ESP_ERROR_CHECK(nvs_set_blob(
	    handle,
	    recall_name,
	    data_in,
	    struct_size
	));
	ESP_ERROR_CHECK(nvs_commit(handle));
	nvs_close(handle);
}

bool get_struct(const char *type, const char *recall_name, void *data_out, size_t *struct_size) {
	nvs_handle_t handle;
	esp_err_t err = nvs_open(type, NVS_READONLY, &handle);
	if (err == ESP_ERR_NVS_NOT_FOUND) {
	    // namespace itself doesn't exist yet (e.g. nothing ever stored)
	    *struct_size = 0;
	    return false;
	}
	ESP_ERROR_CHECK(err);

	err = nvs_get_blob(
	    handle,
	    recall_name,
	    data_out,
	    struct_size
	);
	nvs_close(handle);

	if (err == ESP_ERR_NVS_NOT_FOUND) {
	    *struct_size = 0;
	    return false;
	}
	ESP_ERROR_CHECK(err);
	return true;
}

static void log_app_partition_info() {
    const esp_partition_t *partition = esp_ota_get_running_partition();
    if (partition == NULL) {
        ESP_LOGE("APP", "Could not find running app partition");
        return;
    }

    esp_partition_pos_t part_pos = {
        .offset = partition->address,
        .size = partition->size,
    };

    // image_len is the actual size of the flashed binary (code + embedded
    // assets + appended SHA-256 digest) -- the same number esptool prints
    // as "binary size" after a build, just read back from flash at runtime
    // instead of from the .bin file.
    esp_image_metadata_t metadata;
    if (esp_image_get_metadata(&part_pos, &metadata) != ESP_OK) {
        ESP_LOGE("APP", "Could not read app image metadata");
        return;
    }

    size_t total_bytes = partition->size;
    size_t used_bytes = metadata.image_len;
    size_t free_bytes = total_bytes - used_bytes;

    float usage_percent =
        ((float)used_bytes / (float)total_bytes) * 100.0f;

    printf("\n===== APP INFO =====\n");

    printf("App partition:  %s\n", partition->label);

    printf("Partition size: %zu bytes (%.2f KB)\n",
           total_bytes,
           total_bytes / 1024.0f);

    printf("App image size: %zu bytes (%.2f KB)\n",
           used_bytes,
           used_bytes / 1024.0f);

    printf("App free:       %zu bytes (%.2f KB)\n",
           free_bytes,
           free_bytes / 1024.0f);

    printf("App usage:      %.1f%%\n",
           usage_percent);

    printf("====================\n\n");
}

void disk_init() {
    esp_err_t err = nvs_flash_init();

    if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
        err == ESP_ERR_NVS_NEW_VERSION_FOUND) {

        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }

    ESP_ERROR_CHECK(err);

    // Get NVS statistics
    nvs_stats_t stats;
    ESP_ERROR_CHECK(nvs_get_stats(NULL, &stats));

    // Get NVS partition
    const esp_partition_t *partition =
        esp_partition_find_first(
            ESP_PARTITION_TYPE_DATA,
            ESP_PARTITION_SUBTYPE_DATA_NVS,
            NULL
        );

    if (partition == NULL) {
        ESP_LOGE("NVS", "Could not find NVS partition");
        return;
    }

    // Partition size
    size_t total_bytes = partition->size;

    // Entry usage
    size_t used_entries = stats.used_entries;
    size_t total_entries = stats.total_entries;
    size_t free_entries = stats.free_entries;

    float usage_percent =
        ((float)used_entries / (float)total_entries) * 100.0f;

    printf("\n===== NVS INFO =====\n");

    printf("NVS size:       %zu bytes (%.2f KB)\n",
           total_bytes,
           total_bytes / 1024.0f);

    printf("NVS entries:    %zu / %zu used\n",
           used_entries,
           total_entries);

    printf("NVS free:       %zu entries\n",
           free_entries);

    printf("NVS usage:      %.1f%%\n",
           usage_percent);

    printf("====================\n\n");

    log_app_partition_info();
}

void delete_struct(const char *type, const char *recall_name) {
    nvs_handle_t handle;
    ESP_ERROR_CHECK(nvs_open(type, NVS_READWRITE, &handle));
    esp_err_t err = nvs_erase_key(handle, recall_name);
	if (err == ESP_ERR_NVS_NOT_FOUND) {
	    printf("Struct with name \"%s\" in \"%s\" does not exist.\n", recall_name, type);
	} else {
	    ESP_ERROR_CHECK(err);
	}
    ESP_ERROR_CHECK(nvs_commit(handle));
    nvs_close(handle);
}

void clear_segment(const char *type) {
    nvs_handle_t handle;
    ESP_ERROR_CHECK(nvs_open(type, NVS_READWRITE, &handle));
    ESP_ERROR_CHECK(nvs_erase_all(handle));
    ESP_ERROR_CHECK(nvs_commit(handle));
    nvs_close(handle);
}