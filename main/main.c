#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "disk_interface.h"
#include "wifi_interface.h"
#include "gpio_interface.h"
#include "api_interface.h"
#include "net_setup.h"
#include "eink_driver.h"

#define WIFI_STATUS_PIN (2)

static const char *TAG = "Main";


void wifi_join_state(void *pvParameters)
{
	int current_state = 0;
	gpio_register_output(WIFI_STATUS_PIN);
    while (1) {
        if (wifi_state == WIFI_PENDING) {
        	current_state ^= 1;
        	gpio_power(WIFI_STATUS_PIN, current_state);
        } else if (wifi_state == WIFI_FAILED) {
        	gpio_power(WIFI_STATUS_PIN, false);
        	break;
        } else if (wifi_state == WIFI_CONNECTED) {
        	gpio_power(WIFI_STATUS_PIN, true);
        	break;
        }
        vTaskDelay(pdMS_TO_TICKS(750));
    }
    vTaskDelete(NULL);
}

void store_screen_to_nvm(const char *endpoint, const char *name) {
	ScreenData *screen = create_screen_data_instance();
	api_get(screen, endpoint);
	uint8_t *screen_data = serve_bitmap(screen, 5000);
	if (!screen_data) {
		ESP_LOGE(TAG, "Screen not ready, not storing \"%s\" to NVM", name);
	} else {
		store_struct(SCREEN_TYPE, name, (void *) screen_data, EINK_BUFFER_SIZE);
	}
 
	delete_screen_data_instance(screen);
}
 
bool show_stored_screen(const char *name) {
	uint8_t *screen_buffer = calloc(EINK_BUFFER_SIZE, sizeof(uint8_t));
	if (!screen_buffer) {
		ESP_LOGE(TAG, "Failed to allocate screen buffer");
		return false;
	}

	bool ok = false;
	size_t size = EINK_BUFFER_SIZE; // must be set to capacity before get_struct call
	if (get_struct(SCREEN_TYPE, name, screen_buffer, &size)) {
		ScreenData *screen = create_screen_data_instance_from_mem(screen_buffer, size);
		uint8_t *bitmap = serve_bitmap(screen, 5000);
		ok = bitmap ? display_screen(bitmap) : false;
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

bool display(const char *endpoint) {
    ScreenData *screen = create_screen_data_instance();
    api_get(screen, endpoint);

    uint8_t *bitmap = serve_bitmap(screen, 5000);
    bool ok = bitmap ? display_screen(bitmap) : false;

    delete_screen_data_instance(screen);
    return ok;
}

void system_reset() {
	clear_segment(SCREEN_TYPE);
	// re download system screens
	store_screen_to_nvm("/system/boot.bmp", "boot");
	store_screen_to_nvm("/system/error.bmp", "error");

}


/*
if cannot connect to wifi, flash the qr code page and say "if you think this is wrong reboot"
*/

void app_main(void)
{
	disk_init();
	xTaskCreate(wifi_join_state, "wifi_led", 4096, NULL, 5, NULL);
	eink_init();
	boot_screen();
	wifi_init();

	WifiDetails stored_wifi;
	size_t size;
	if (get_struct(CONFIG_TYPE, "wifi", &stored_wifi, &size)) {
		if (!wifi_join(stored_wifi.ssid, stored_wifi.pwd)) {
			error_screen();
			return;
		}
	} else {
		wifi_state = WIFI_FAILED;
		error_screen();
		return;
	}

	// Stall here until the clock is trustworthy: mbedTLS checks certificate
	// notBefore/notAfter against the wall clock, so HTTPS requests made before
	// this point are liable to fail (or worse, silently accept a bad cert).
	if (!net_setup_wait_ready()) {
		ESP_LOGE(TAG, "Network never became ready, refusing to make requests");
		error_screen();
		return;
	}
	// update screens if system was told to
	if (CONFIG_UPDATE_SYSTEM_SCREENS) {
		system_reset();
	}
	

	display("/main/pacific.bmp");
}
