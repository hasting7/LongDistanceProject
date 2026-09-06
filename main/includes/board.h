#ifndef _BOARD_H_
#define _BOARD_H_

/*
 * Physical layout of this board: GPIO pin assignments and the e-ink
 * panel's fixed geometry. This describes hardware, not behavior -- if
 * the panel or wiring ever changes, this is the only file that needs
 * to change with it.
 */

// ============================================================
// E-ink panel (SSD1680, 296x128 physical, RAM packed 128x296)
// ============================================================
#define EINK_PIN_MOSI   23
#define EINK_PIN_SCLK   18
#define EINK_PIN_CS      5
#define EINK_PIN_DC     17
#define EINK_PIN_RST    16
#define EINK_PIN_BUSY    4

#define EINK_WIDTH          (128)  // X direction, packed 8 px/byte -> 16 bytes/row
#define EINK_HEIGHT         (296)  // Y direction, one line per RAM row
#define EINK_WIDTH_BYTES    (EINK_WIDTH / 8)
#define EINK_BUFFER_SIZE    (EINK_WIDTH_BYTES * EINK_HEIGHT)

// BMP pixel rows are padded to a multiple of 4 bytes; how much padding
// follows each row of this panel's fixed width.
#define EINK_ROW_PADDING    ((4 - (EINK_WIDTH_BYTES % 4)) % 4)

// ============================================================
// Other board I/O
// ============================================================
#define WIFI_STATUS_PIN         (2)
#define PROVISION_BUTTON_GPIO   (0)

#endif
