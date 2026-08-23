
#include <stdio.h> // REMOVE
#include <string.h>


#define EINK_WIDTH			(128)
#define EINK_HEIGHT 		(296)
#define EINK_WIDTH_BYTES 	(EINK_WIDTH / 8) 
#define EINK_BUFFER_SIZE 	(EINK_WIDTH_BYTES * EINK_HEIGHT)

#define MIN(a, b) ((a) < (b) ? (a) : (b))

static uint8_t bitmap_buffer[EINK_BUFFER_SIZE];
static int width_padding_bytes = 0;
static int used_buff = 0;
static int used_bitmap_row_bytes = 0;
static int total_bitmap_size = 0;
static int image_width = 0;
static int image_height = 0;
static int should_skip = 0;


/*
FIX this file
just start the buffer in the heap, not just on the return
it is overflowing the stack so just init on the stack inistally 
*/

static uint32_t read_u32_le(const char *buf) {
    return  (uint32_t)buf[0]
          | (uint32_t)buf[1] << 8
          | (uint32_t)buf[2] << 16
          | (uint32_t)buf[3] << 24;
}

static int reset_bitmap_buffer(const char *buffer) {
	int header_offset_size = 0;

	used_buff = 0;
	total_bitmap_size = 0;
	image_width = 0;
	image_height = 0;
	used_bitmap_row_bytes = 0;
	should_skip = 0;

	while ((width_padding_bytes + EINK_WIDTH_BYTES) % 4 != 0) {
		width_padding_bytes += 1;
	}
	printf("Padding bytes: %d\n",width_padding_bytes);

	// read though header

	// bfType
	buffer += 2;

	// bfSize
	total_bitmap_size = read_u32_le(buffer);
	printf("Total bitmap payload size: %d\n", total_bitmap_size);
	buffer += 4;

	// unused
	buffer += 4;

	// get header offset 
	header_offset_size = read_u32_le(buffer);
	buffer += 4;

	// unused
	buffer += 4;

	// get image width 
	image_width = read_u32_le(buffer);
	buffer += 4;

	// get image height
	image_height = read_u32_le(buffer);
	buffer += 4;

	printf("total bitmap size: %d\nimage width: %d\nimage height: %d\n", total_bitmap_size, image_width, image_height);

	return header_offset_size;
}

uint8_t* serve_bitmap() {
	if (used_buff != EINK_BUFFER_SIZE) {
		printf("NOT COMPLETE\n");
		return NULL;
	}
	uint8_t *ptr = calloc(EINK_BUFFER_SIZE, sizeof(uint8_t));
	memcpy(ptr, bitmap_buffer, EINK_BUFFER_SIZE);
	return ptr;
}

void consume_http_packet(const char *buffer, size_t buffer_size) {
	int consumed_buffer = 0;
	int bytes_to_read;

	if (buffer[0] == 'B' && buffer[1] == 'M') {
		// new image
		consumed_buffer = reset_bitmap_buffer(buffer);
	}
	// read into buffer
	printf("Buffer size in : %d\n", buffer_size);
	printf("Copying %d bytes to bufer.\n", buffer_size - consumed_buffer);
	// read until used_buff == EINK_WIDTH_BYTES, then discard width_padding_bytes

	while (consumed_buffer < buffer_size) {
		if (should_skip > 0) {
			consumed_buffer++;
			should_skip--;
			continue;
		}
		// get bytes to read
		bytes_to_read = MIN(EINK_WIDTH_BYTES - used_bitmap_row_bytes, buffer_size - consumed_buffer);
		printf("reading %d bytes into buffer.\n", bytes_to_read);

		// read the bytes
		memcpy(bitmap_buffer + used_buff, buffer + consumed_buffer, bytes_to_read);
		consumed_buffer += bytes_to_read;
		used_bitmap_row_bytes += bytes_to_read;
		used_buff += bytes_to_read;

		if (EINK_WIDTH_BYTES == used_bitmap_row_bytes) {
			used_bitmap_row_bytes = 0;
			should_skip += width_padding_bytes;
		}

	}

	printf("%d / %d filled\n", used_buff, EINK_BUFFER_SIZE);

}