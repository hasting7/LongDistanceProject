#include "disk_interface.h"

#include "nvs_flash.h"
#include "nvs.h"

const char* CONFIG_TYPE = "config";
const char* USER_TYPE = "user";

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
	ESP_ERROR_CHECK(nvs_open(type, NVS_READONLY, &handle));

	esp_err_t err = nvs_get_blob(
	    handle,
	    recall_name,
	    data_out,
	    struct_size
	);

	nvs_close(handle);

	if (err == ESP_ERR_NVS_NOT_FOUND) {
	    data_out = NULL;
	    *struct_size = 0;
	    return false;
	}
	return true;
}

void disk_init() {
	esp_err_t err = nvs_flash_init();
	if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
		ESP_ERROR_CHECK(nvs_flash_erase());
		err = nvs_flash_init();
	}
	ESP_ERROR_CHECK(err);
}

void delete_struct(const char *type, const char *recall_name) {
    nvs_handle_t handle;

    ESP_ERROR_CHECK(nvs_open(type, NVS_READWRITE, &handle));

    esp_err_t err = nvs_erase_key(handle, recall_name);

	if (err == ESP_ERR_NVS_NOT_FOUND) {
	    printf("Struct with name \"%s\" in \"%s\" does not exist.\n",recall_name, type);
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