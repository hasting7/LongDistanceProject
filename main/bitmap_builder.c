#include <string.h>
#include "sdkconfig.h"


#include "esp_log.h"
#include "esp_system.h"
#include "bitmap_builder.h"

#define EINK_WIDTH			(128)
#define EINK_HEIGHT 		(296)
#define EINK_WIDTH_BYTES 	(EINK_WIDTH / 8)

const int EINK_BUFFER_SIZE = (EINK_WIDTH_BYTES * EINK_HEIGHT);
#define MIN(a, b) ((a) < (b) ? (a) : (b))

static const char *TAG = "BitMap Builder";
static int width_padding_bytes = 0;


typedef struct screen_data_t{
	uint8_t *bitmap_buffer;
	ScreenStatus status;
	int consumed_buffer;
	int row_consumed;
	int total_bitmap_size;
	int image_width;
	int image_height;
	int skipping;
	SemaphoreHandle_t ready_sem;
} ScreenData;

ScreenData *create_screen_data_instance(uint8_t *existing_buffer) {
	ScreenData *ptr = malloc(sizeof(ScreenData));

	if (!existing_buffer) {
		ptr->bitmap_buffer = calloc(EINK_BUFFER_SIZE, sizeof(uint8_t));
	} else {
		ptr->bitmap_buffer = existing_buffer;
		// ADD CHECKS TO MAKE SURE BUFFER IS BIG ENOUGH
	}
	ptr->consumed_buffer = 0;
	ptr->row_consumed = 0;
	ptr->total_bitmap_size = 0;
	ptr->image_width = 0;
	ptr->image_height = 0;
	ptr->skipping = 0;
	ptr->status = NOT_STARTED;
	ptr->ready_sem = xSemaphoreCreateBinary();
	return ptr;

}

ScreenData *create_screen_data_instance_from_mem(uint8_t *bitmap_buffer, size_t size) {
	ScreenData *ptr = malloc(sizeof(ScreenData));
	ptr->bitmap_buffer = bitmap_buffer;
	ptr->status = COMPLETE;
	ptr->ready_sem = xSemaphoreCreateBinary();
	xSemaphoreGive(ptr->ready_sem);
	return ptr;
}

void delete_screen_data_instance(ScreenData *ptr) {
	vSemaphoreDelete(ptr->ready_sem);
	free(ptr->bitmap_buffer);
	free(ptr);
	ptr = NULL;
}


static uint32_t read_u32_le(const char *buf) {
    return  (uint32_t)buf[0]
          | (uint32_t)buf[1] << 8
          | (uint32_t)buf[2] << 16
          | (uint32_t)buf[3] << 24;
}

/*
	REJECT REQUESTS THAT DONT HAVE THE SAME INAGES SIZE OF EXPECTED
*/
static int reset_bitmap_buffer(ScreenData *screen, const char *packet_buffer) {
	int header_offset_size = 0;

	screen->consumed_buffer = 0;
	screen->row_consumed = 0;
	screen->total_bitmap_size = 0;
	screen->image_width = 0;
	screen->image_height = 0;
	screen->skipping = 0;
	screen->status = IN_PROGRESS;

	// MOVE THIS
	while ((width_padding_bytes + EINK_WIDTH_BYTES) % 4 != 0) {
		width_padding_bytes += 1;
	}
	ESP_LOGI(TAG, "Padding bytes: %d", width_padding_bytes);

	// read though header

	// bfType
	packet_buffer += 2;

	// bfSize
	screen->total_bitmap_size = read_u32_le(packet_buffer);
	packet_buffer += 4;

	// unused
	packet_buffer += 4;

	// get header offset 
	header_offset_size = read_u32_le(packet_buffer);
	packet_buffer += 4;

	// unused
	packet_buffer += 4;

	// get image width 
	screen->image_width = read_u32_le(packet_buffer);
	packet_buffer += 4;

	// get image height
	screen->image_height = read_u32_le(packet_buffer);
	packet_buffer += 4;

	ESP_LOGI(TAG, "total bitmap size: %d, image width: %d, image height: %d",
	         screen->total_bitmap_size, screen->image_width, screen->image_height);

	if (screen->image_width != EINK_WIDTH || screen->image_height != EINK_HEIGHT) {
		ESP_LOGE(TAG, "Rejecting bitmap: got %dx%d, expected %dx%d",
		         screen->image_width, screen->image_height, EINK_WIDTH, EINK_HEIGHT);
		screen->status = REJECTED;
		xSemaphoreGive(screen->ready_sem);
		return -1;
	}

	return header_offset_size;
}

uint8_t* serve_bitmap(ScreenData *screen, uint32_t timeout_ms) {
    if (xSemaphoreTake(screen->ready_sem, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        ESP_LOGE(TAG, "Timed out waiting for bitmap to complete");
        return NULL;
    }
    if (screen->status != COMPLETE) {
        ESP_LOGW(TAG, "Bitmap not usable, status=%d", screen->status);
        return NULL;
    }
    return screen->bitmap_buffer;
}

void consume_http_packet(ScreenData *screen, const char *packet_buffer, size_t buffer_size) {
	int consumed_buffer = 0;
	int bytes_to_read;

	if (screen->status == REJECTED) {
		// already rejected this screen (bad dimensions / oversized payload);
		// ignore whatever is left of the response body.
		return;
	}

	if (packet_buffer[0] == 'B' && packet_buffer[1] == 'M') {
		if (screen->status != NOT_STARTED) {
			// why are we getting another packet start if we are in progress
			ESP_LOGE(TAG, "Screen already buildng but intro packet received.");
		}
		// new image
		consumed_buffer = reset_bitmap_buffer(screen, packet_buffer);
		if (consumed_buffer < 0) {
			return;
		}
	}
	// read into buffer
	ESP_LOGI(TAG, "Buffer size in: %d, copying %d bytes to buffer.", buffer_size, buffer_size - consumed_buffer);

	while (consumed_buffer < buffer_size) {
		if (screen->skipping > 0) {
			consumed_buffer++;
			screen->skipping--;
			continue;
		}

		int remaining_space = EINK_BUFFER_SIZE - screen->consumed_buffer;
		if (remaining_space <= 0) {
			ESP_LOGE(TAG, "Bitmap payload exceeds expected buffer size (%d bytes), rejecting", EINK_BUFFER_SIZE);
			screen->status = REJECTED;
			xSemaphoreGive(screen->ready_sem);
			return;
		}

		// get bytes to read
		bytes_to_read = MIN(EINK_WIDTH_BYTES - screen->row_consumed, buffer_size - consumed_buffer);
		bytes_to_read = MIN(bytes_to_read, remaining_space);

		// read the bytes
		memcpy(screen->bitmap_buffer + screen->consumed_buffer, packet_buffer + consumed_buffer, bytes_to_read);
		consumed_buffer += bytes_to_read;
		screen->row_consumed += bytes_to_read;
		screen->consumed_buffer += bytes_to_read;

		if (EINK_WIDTH_BYTES == screen->row_consumed) {
			screen->row_consumed = 0;
			screen->skipping += width_padding_bytes;
		}

	}
	if (screen->consumed_buffer == EINK_BUFFER_SIZE) {
		screen->status = COMPLETE;
		xSemaphoreGive(screen->ready_sem);
	}

	ESP_LOGI(TAG, "%d / %d filled", screen->consumed_buffer, EINK_BUFFER_SIZE);

}