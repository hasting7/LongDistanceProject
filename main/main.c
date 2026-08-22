#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "disk_interface.h"
#include "wifi_interface.h"
#include "gpio_interface.h"
#include "api_interface.h"

#define WIFI_STATUS_PIN (2)


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

void app_main(void)
{
	disk_init();
	wifi_init();
	
	xTaskCreatePinnedToCore(wifi_join_state, "wifi_led", 4096, NULL, 5, NULL, 1);

	WifiDetails stored_wifi;
	size_t size;
	if (get_struct(CONFIG_TYPE, "wifi", &stored_wifi, &size)) {
		if (!wifi_join(stored_wifi.ssid, stored_wifi.pwd)) {
			return;
		}
	} else {
		wifi_state = WIFI_FAILED;
	}

	api_get();



	// clear_segment(USER_TYPE)

	// WifiDetails default_wifi = { .ssid = "Hastings Wifi", .pwd = "M1212hS0701h_"};
	// store_struct(CONFIG_TYPE, "wifi", (void *) &default_wifi, sizeof(default_wifi));

	// WifiDetails stored_info;
	// size_t size;
	// get_struct(CONFIG_TYPE, "wifi", &stored_info, &size);

	// if (!size) {
	// 	printf("No wifi found...\n");
	// 	return;
	// } else {
	// 	printf("Wifi found\n\tssid: %s\n\tpwd: %s\n", stored_info.ssid, stored_info.pwd);
	// 	if (!wifi_join(stored_info.ssid, stored_info.pwd)) {
	// 		printf("Could not connect to wifi...\n");
	// 		return;
	// 	}
	// }

	// printf("Successfully running....\n");



	// if (!wifi_join("Hastings Wifi", "M1212hS0701h_")) {
	// 	printf("Could not join WiFi -- falling back\n");
	// }

	// store_struct(USER_TYPE, "name", (void *) "Ben", 3);

	// char buff[4];
	// size_t bytes = get_struct(USER_TYPE,"name2", buff, 4);
	// buff[3] ='\0';
	// if (!bytes) {
	// 	printf("Nothing found\n");
	// } else {
	// 	printf("NAME FROM DISK: %s (%d)\n", buff, bytes);		
	// }

}
