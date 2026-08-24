#ifndef _EINK_DRIVER_H_
#define _EINK_DRIVER_H_

#include "bitmap_builder.h"

void eink_init(void);
bool display_screen(uint8_t *ptr);

#endif