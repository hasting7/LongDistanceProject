#ifndef _EINK_DRIVER_H_
#define _EINK_DRIVER_H_

void eink_init(void);
void draw_bmp(const uint8_t *framebuf, size_t size);

#endif