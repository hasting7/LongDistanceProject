#ifndef _BITMAP_BUILDER_H_
#define _BITMAP_BUILDER_H_

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

typedef struct screen_data_t ScreenData;

typedef enum {
	NOT_STARTED,
	IN_PROGRESS,
	COMPLETE,
	REJECTED
} ScreenStatus;

extern const int EINK_BUFFER_SIZE;
ScreenData *create_screen_data_instance(uint8_t *existing_buffer);
ScreenData *create_screen_data_instance_from_mem(uint8_t *bitmap_buffer, size_t size);
void delete_screen_data_instance(ScreenData *ptr);
uint8_t* serve_bitmap(ScreenData *screen, uint32_t timeout);
void consume_http_packet(ScreenData *screen, const char *packet_buffer, size_t buffer_size);

#endif