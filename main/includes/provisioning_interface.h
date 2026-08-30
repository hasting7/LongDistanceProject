#ifndef _PROVISIONING_INTERFACE_H_
#define _PROVISIONING_INTERFACE_H_

#include <stdbool.h>

/*
 * Hosts a SoftAP named CONFIG_DEVICE_NAME along with a captive portal, and
 * blocks the calling task until a client submits home WiFi credentials.
 *
 * On submit the credentials are written to NVS as a WifiDetails blob under
 * CONFIG_TYPE / "wifi" -- the exact layout app_main reads back for wifi_join().
 *
 * Must be called after wifi_init(), which owns esp_netif_init(), the default
 * event loop and esp_wifi_init(). Before returning, this function tears the
 * AP and portal down completely (httpd, DNS hijack, radio, AP netif) and
 * leaves WiFi in mode STA, not started -- i.e. exactly the state wifi_init()
 * originally left it in. No reboot is needed: the caller can call
 * wifi_join() immediately afterward. Returns false if the AP or the portal
 * could not be brought up, in which case nothing is left running.
 */
bool provisioning_start(void);

#endif
