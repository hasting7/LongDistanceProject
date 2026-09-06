#ifndef _API_INTERFACE_H_
#define _API_INTERFACE_H_

#include "bitmap_builder.h"


#if CONFIG_SERVER_ENVIRONMENT_CLOUD
#define SERVER_NAME CONFIG_SERVER_CLOUD_IP
#else
#define SERVER_NAME CONFIG_SERVER_LOCAL_IP
#endif


void api_get(ScreenData *screen, const char *url);
bool cloud_check_id(const char *endpoint, uint8_t *out_id);

#endif