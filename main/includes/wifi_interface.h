#ifndef _WIFI_INTERFACE_H_
#define _WIFI_INTERFACE_H_

#include <stdbool.h>

void wifi_init(void);
bool wifi_join(const char *ssid, const char *password);

typedef struct {
    char ssid[64];
    char pwd[64];
} WifiDetails;

typedef enum {
    WIFI_PENDING,
    WIFI_CONNECTED,
    WIFI_FAILED,
} wifi_state_enum;

extern volatile wifi_state_enum wifi_state;

#endif