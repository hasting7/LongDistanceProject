#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_http_server.h"

#include "lwip/sockets.h"
#include "lwip/inet.h"

#include "sdkconfig.h"

#include "disk_interface.h"
#include "wifi_interface.h"
#include "provisioning_interface.h"

static const char *TAG = "provisioning";

#define PROV_DONE_BIT BIT0
#define DNS_PORT      53
#define AP_IP_ADDR    "192.168.4.1"

/* esp_netif.h doesn't re-export lwip's dhcpserver.h, so the option value
 * (dhcpserver.h's OFFER_DNS bit) has to be duplicated here -- this mirrors
 * IDF's own examples/wifi/softap_sta, which does the same thing. */
#define DHCPS_OFFER_DNS 0x02

static EventGroupHandle_t s_prov_event_group;
static httpd_handle_t s_httpd = NULL;
static esp_netif_t *s_ap_netif = NULL;

static volatile bool s_dns_run = false;
static int s_dns_sock = -1;

/* Embedded via EMBED_TXTFILES in main/CMakeLists.txt; the _start pointer is
 * null-terminated so HTTPD_RESP_USE_STRLEN is safe to use with it. */
extern const char index_html_start[] asm("_binary_index_html_start");
extern const char style_css_start[] asm("_binary_style_css_start");

static const char SAVED_HTML[] =
    "<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
    "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
    "<title>Saved</title></head>"
    "<body style=\"font-family:sans-serif;text-align:center;padding-top:3rem;\">"
    "<h1>Saved</h1><p>The device will now connect to your network.</p>"
    "</body></html>";

/* httpd_query_key_value() splits key/value pairs but never URL-decodes them,
 * so this rewrites a string in place: "%XX" -> byte, "+" -> space. */
static void url_decode(char *s)
{
    char *o = s;
    while (*s) {
        if (*s == '%' && s[1] && s[2]) {
            char hex[3] = { s[1], s[2], 0 };
            *o++ = (char)strtol(hex, NULL, 16);
            s += 3;
        } else if (*s == '+') {
            *o++ = ' ';
            s++;
        } else {
            *o++ = *s++;
        }
    }
    *o = '\0';
}

static bool start_ap(void)
{
    s_ap_netif = esp_netif_create_default_wifi_ap();
    if (!s_ap_netif) {
        ESP_LOGE(TAG, "Failed to create AP netif");
        return false;
    }

    /*
     * AP-only, deliberately not APSTA: wifi_interface.c's event handler
     * calls esp_wifi_connect() unconditionally on WIFI_EVENT_STA_START,
     * which would fire in APSTA mode and burn through the retry budget
     * against whatever (possibly stale) STA config is loaded.
     */
    if (esp_wifi_set_mode(WIFI_MODE_AP) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set WiFi mode to AP");
        return false;
    }

    wifi_config_t cfg = { 0 };
    strlcpy((char *)cfg.ap.ssid, CONFIG_DEVICE_NAME, sizeof(cfg.ap.ssid));
    cfg.ap.ssid_len = strlen((char *)cfg.ap.ssid);
    cfg.ap.channel = 1;
    cfg.ap.max_connection = 4;
    cfg.ap.beacon_interval = 100;

    if (strlen(CONFIG_DEVICE_PASSWORD) >= 8) {
        strlcpy((char *)cfg.ap.password, CONFIG_DEVICE_PASSWORD, sizeof(cfg.ap.password));
        cfg.ap.authmode = WIFI_AUTH_WPA2_PSK;
    } else {
        ESP_LOGW(TAG, "CONFIG_DEVICE_PASSWORD is shorter than 8 characters, starting an open AP");
        cfg.ap.authmode = WIFI_AUTH_OPEN;
    }

    if (esp_wifi_set_config(WIFI_IF_AP, &cfg) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set AP config");
        return false;
    }

    if (esp_wifi_start() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start WiFi in AP mode");
        return false;
    }

    /*
     * Make the AP hand out our own address as the DNS server, so a phone's
     * captive-portal check resolves straight to us instead of timing out.
     * Without ESP_NETIF_DHCPS_OFFER_DNS the DHCP offer carries no DNS
     * server at all and the hijack task below never sees a query.
     */
    esp_netif_dhcps_stop(s_ap_netif);

    esp_netif_dns_info_t dns = { 0 };
    dns.ip.type = ESP_IPADDR_TYPE_V4;
    dns.ip.u_addr.ip4.addr = inet_addr(AP_IP_ADDR);
    esp_netif_set_dns_info(s_ap_netif, ESP_NETIF_DNS_MAIN, &dns);

    uint8_t dns_offer = DHCPS_OFFER_DNS;
    esp_netif_dhcps_option(s_ap_netif, ESP_NETIF_OP_SET, ESP_NETIF_DOMAIN_NAME_SERVER,
                            &dns_offer, sizeof(dns_offer));

    esp_netif_dhcps_start(s_ap_netif);

    ESP_LOGI(TAG, "Provisioning AP \"%s\" up, portal at http://" AP_IP_ADDR "/", CONFIG_DEVICE_NAME);
    return true;
}

static esp_err_t root_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, index_html_start, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t style_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/css");
    httpd_resp_send(req, style_css_start, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

/* Holds a single form value straight out of the POST body, still
 * percent-encoded -- httpd_query_key_value() doesn't decode, so this has to
 * fit the *encoded* length (up to 3x the decoded one: every byte can become
 * "%XX"), not just the final decoded length. 200 comfortably covers a fully
 * escaped 63-char password (189 bytes) plus a NUL. */
#define RAW_FIELD_MAX 200

static esp_err_t connect_post_handler(httpd_req_t *req)
{
    char body[600]; /* ssid + pwd + pwd_confirm, each up to RAW_FIELD_MAX encoded */

    if (req->content_len == 0 || req->content_len >= sizeof(body)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Bad form length");
        return ESP_FAIL;
    }

    size_t received = 0;
    while (received < req->content_len) {
        int r = httpd_req_recv(req, body + received, req->content_len - received);
        if (r <= 0) {
            if (r == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;
            }
            return ESP_FAIL;
        }
        received += r;
    }
    body[received] = '\0';

    char ssid[RAW_FIELD_MAX] = { 0 };
    char pwd[RAW_FIELD_MAX] = { 0 };
    char pwd_confirm[RAW_FIELD_MAX] = { 0 };

    bool have_ssid = httpd_query_key_value(body, "ssid", ssid, sizeof(ssid)) == ESP_OK;
    httpd_query_key_value(body, "pwd", pwd, sizeof(pwd)); /* an empty password is fine */
    httpd_query_key_value(body, "pwd_confirm", pwd_confirm, sizeof(pwd_confirm));

    url_decode(ssid);
    url_decode(pwd);
    url_decode(pwd_confirm);

    if (!have_ssid || strlen(ssid) == 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Network name is required");
        return ESP_FAIL;
    }

    if (strcmp(pwd, pwd_confirm) != 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Passwords do not match");
        return ESP_FAIL;
    }

    WifiDetails details = { 0 };
    strlcpy(details.ssid, ssid, sizeof(details.ssid));
    strlcpy(details.pwd, pwd, sizeof(details.pwd));
    store_struct(CONFIG_TYPE, "wifi", &details, sizeof(details));

    ESP_LOGI(TAG, "Stored credentials for SSID \"%s\", password \"%s\"", details.ssid, details.pwd);

    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, SAVED_HTML, HTTPD_RESP_USE_STRLEN);

    /* Signal completion only after the response is handed to the socket, so
     * the caller doesn't tear the AP down mid-flush. */
    xEventGroupSetBits(s_prov_event_group, PROV_DONE_BIT);

    return ESP_OK;
}

/* Catches every other URL, including the captive-portal probes phones and
 * laptops send (/generate_204, /hotspot-detect.html, /ncsi.txt, ...), and
 * bounces all of them at the portal. Must be registered last: httpd walks
 * handlers in registration order and this wildcard would otherwise swallow
 * everything above it. */
static esp_err_t redirect_get_handler(httpd_req_t *req)
{
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://" AP_IP_ADDR "/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

static bool start_httpd(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 8192; /* default 4096 is tight once NVS writes are in the call stack */
    config.max_uri_handlers = 8;
    config.lru_purge_enable = true; /* phones open several probe sockets at once */
    config.uri_match_fn = httpd_uri_match_wildcard;

    if (httpd_start(&s_httpd, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start httpd");
        return false;
    }

    httpd_uri_t root_uri = { .uri = "/", .method = HTTP_GET, .handler = root_get_handler };
    httpd_uri_t style_uri = { .uri = "/style.css", .method = HTTP_GET, .handler = style_get_handler };
    httpd_uri_t connect_uri = { .uri = "/connect", .method = HTTP_POST, .handler = connect_post_handler };
    httpd_uri_t redirect_uri = { .uri = "/*", .method = HTTP_GET, .handler = redirect_get_handler };

    httpd_register_uri_handler(s_httpd, &root_uri);
    httpd_register_uri_handler(s_httpd, &style_uri);
    httpd_register_uri_handler(s_httpd, &connect_uri);
    httpd_register_uri_handler(s_httpd, &redirect_uri); /* must stay last */

    return true;
}

/* Answers every DNS query with the AP's own address, which is what makes a
 * phone's captive-portal probe auto-open the sign-in page. */
static void dns_task(void *arg)
{
    s_dns_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s_dns_sock < 0) {
        ESP_LOGE(TAG, "Failed to create DNS socket");
        vTaskDelete(NULL);
        return;
    }

    struct sockaddr_in addr = { 0 };
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(DNS_PORT);

    if (bind(s_dns_sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        ESP_LOGE(TAG, "Failed to bind DNS socket to port %d", DNS_PORT);
        close(s_dns_sock);
        s_dns_sock = -1;
        vTaskDelete(NULL);
        return;
    }

    uint8_t buf[512];
    uint8_t reply[512];

    while (s_dns_run) {
        struct sockaddr_in from;
        socklen_t from_len = sizeof(from);

        int len = recvfrom(s_dns_sock, buf, sizeof(buf), 0, (struct sockaddr *)&from, &from_len);
        if (len <= 0) {
            break; /* socket was shut down out from under us -> stop_dns_task() is tearing down */
        }
        if (len < 12) {
            continue; /* shorter than a DNS header, ignore */
        }
        if ((buf[2] & 0x80) != 0) {
            continue; /* QR bit set: this is a response, not a query */
        }
        uint16_t qdcount = (buf[4] << 8) | buf[5];
        if (qdcount != 1) {
            continue; /* only handle the single-question case every real client sends */
        }

        /* Walk past the question's label sequence (terminated by a 0 byte),
         * then its QTYPE + QCLASS, to find where the answer goes. */
        int pos = 12;
        while (pos < len && buf[pos] != 0) {
            pos += buf[pos] + 1;
        }
        pos += 1 + 4;
        if (pos > len || pos + 16 > (int)sizeof(reply)) {
            continue;
        }

        memcpy(reply, buf, pos);

        reply[2] = 0x84; /* QR=1, Opcode=0, AA=1, TC=0, RD=0 */
        reply[3] = 0x00; /* RA=0, Z=0, RCODE=0 */
        reply[6] = 0x00; reply[7] = 0x01; /* ANCOUNT = 1 */
        reply[8] = 0x00; reply[9] = 0x00; /* NSCOUNT = 0 */
        reply[10] = 0x00; reply[11] = 0x00; /* ARCOUNT = 0 */

        int rpos = pos;
        reply[rpos++] = 0xC0; reply[rpos++] = 0x0C; /* name: pointer to question at offset 12 */
        reply[rpos++] = 0x00; reply[rpos++] = 0x01; /* TYPE = A */
        reply[rpos++] = 0x00; reply[rpos++] = 0x01; /* CLASS = IN */
        reply[rpos++] = 0x00; reply[rpos++] = 0x00; reply[rpos++] = 0x00; reply[rpos++] = 0x3C; /* TTL = 60s */
        reply[rpos++] = 0x00; reply[rpos++] = 0x04; /* RDLENGTH = 4 */

        uint32_t ap_ip = inet_addr(AP_IP_ADDR);
        memcpy(&reply[rpos], &ap_ip, 4);
        rpos += 4;

        sendto(s_dns_sock, reply, rpos, 0, (struct sockaddr *)&from, from_len);
    }

    close(s_dns_sock);
    s_dns_sock = -1;
    vTaskDelete(NULL);
}

static void start_dns_task(void)
{
    s_dns_run = true;
    xTaskCreate(dns_task, "prov_dns", 4096, NULL, 5, NULL);
}

static void stop_dns_task(void)
{
    s_dns_run = false;
    if (s_dns_sock >= 0) {
        /* Unblocks the task's recvfrom(); the task closes the fd itself and
         * self-deletes once it observes the shutdown. */
        shutdown(s_dns_sock, SHUT_RDWR);
    }
    vTaskDelay(pdMS_TO_TICKS(100));
}

bool provisioning_start(void)
{
    s_prov_event_group = xEventGroupCreate();
    if (!s_prov_event_group) {
        ESP_LOGE(TAG, "Failed to create provisioning event group");
        return false;
    }

    if (!start_ap()) {
        vEventGroupDelete(s_prov_event_group);
        s_prov_event_group = NULL;
        return false;
    }

    if (!start_httpd()) {
        vEventGroupDelete(s_prov_event_group);
        s_prov_event_group = NULL;
        return false;
    }

    start_dns_task();

    ESP_LOGI(TAG, "Waiting for WiFi credentials via the captive portal...");
    xEventGroupWaitBits(s_prov_event_group, PROV_DONE_BIT, pdFALSE, pdFALSE, portMAX_DELAY);

    /* Give the "saved" response time to actually reach the browser before
     * tearing the AP down out from under the socket. */
    vTaskDelay(pdMS_TO_TICKS(1500));

    stop_dns_task();
    httpd_stop(s_httpd);
    s_httpd = NULL;

    /*
     * Unwind all the way back to the state wifi_init() originally left
     * things in (mode STA, driver not started) so the caller can go
     * straight into wifi_join() with no reboot. The mode can only be
     * changed cleanly once the driver is stopped -- the same reason
     * start_ap() set AP mode before its own esp_wifi_start().
     */
    esp_wifi_stop();
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_netif_destroy_default_wifi(s_ap_netif);
    s_ap_netif = NULL;

    vEventGroupDelete(s_prov_event_group);
    s_prov_event_group = NULL;

    ESP_LOGI(TAG, "Provisioning portal torn down");
    return true;
}
