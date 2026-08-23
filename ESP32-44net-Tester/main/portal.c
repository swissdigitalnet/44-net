#include <string.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "portal.h"
#include "netcfg.h"
#include "esp_wifi.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "portal";
#define PORTAL_IP "192.168.4.1"

static httpd_handle_t s_httpd;
static volatile bool s_saved;
static volatile bool s_dns_run;

/* ---------- DNS: answer every A query with the portal address ---------- */

static void dns_task(void *arg)
{
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        ESP_LOGE(TAG, "dns socket failed");
        vTaskDelete(NULL);
        return;
    }

    struct sockaddr_in me = {
        .sin_family      = AF_INET,
        .sin_port        = htons(53),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    if (bind(sock, (struct sockaddr *)&me, sizeof(me)) < 0) {
        ESP_LOGE(TAG, "dns bind failed");
        close(sock);
        vTaskDelete(NULL);
        return;
    }

    struct timeval tv = { .tv_sec = 1, .tv_usec = 0 };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    uint8_t buf[512];
    while (s_dns_run) {
        struct sockaddr_in from;
        socklen_t fl = sizeof(from);
        int n = recvfrom(sock, buf, sizeof(buf), 0, (struct sockaddr *)&from, &fl);
        if (n < 12) {
            continue;
        }
        if (n + 16 > (int)sizeof(buf)) {
            continue;
        }

        /* Reply to the question verbatim, appending one A record pointing here. */
        buf[2] |= 0x80;    /* QR = response */
        buf[3]  = 0x00;    /* RCODE 0, not authoritative */
        buf[6]  = 0x00;
        buf[7]  = 0x01;    /* ANCOUNT = 1 */
        buf[8]  = 0x00;
        buf[9]  = 0x00;
        buf[10] = 0x00;
        buf[11] = 0x00;

        uint8_t *a = buf + n;
        *a++ = 0xC0; *a++ = 0x0C;               /* name: pointer to the question */
        *a++ = 0x00; *a++ = 0x01;               /* TYPE  A  */
        *a++ = 0x00; *a++ = 0x01;               /* CLASS IN */
        *a++ = 0x00; *a++ = 0x00; *a++ = 0x00; *a++ = 0x3C;   /* TTL 60 */
        *a++ = 0x00; *a++ = 0x04;               /* RDLENGTH */
        uint32_t ip = inet_addr(PORTAL_IP);
        memcpy(a, &ip, 4);
        a += 4;

        sendto(sock, buf, a - buf, 0, (struct sockaddr *)&from, fl);
    }

    close(sock);
    vTaskDelete(NULL);
}

/* ---------- form helpers ---------- */

static void url_decode(char *s)
{
    char *o = s;
    for (char *p = s; *p; p++) {
        if (*p == '+') {
            *o++ = ' ';
        } else if (*p == '%' && p[1] && p[2]) {
            char h[3] = { p[1], p[2], 0 };
            *o++ = (char)strtol(h, NULL, 16);
            p += 2;
        } else {
            *o++ = *p;
        }
    }
    *o = 0;
}

static bool form_field(const char *body, const char *key, char *out, size_t out_len)
{
    char pat[32];
    snprintf(pat, sizeof(pat), "%s=", key);

    const char *p = body;
    while ((p = strstr(p, pat)) != NULL) {
        /* must be at the start or just after a separator, so ssid= does not
           also match ssid_manual= */
        if (p == body || p[-1] == '&') {
            break;
        }
        p += strlen(pat);
    }
    if (!p) {
        return false;
    }

    p += strlen(pat);
    const char *e = strchr(p, '&');
    size_t n = e ? (size_t)(e - p) : strlen(p);
    if (n >= out_len) {
        n = out_len - 1;
    }
    memcpy(out, p, n);
    out[n] = 0;
    url_decode(out);
    return true;
}

/* ---------- pages ---------- */

static esp_err_t root_get(httpd_req_t *req)
{
    uint16_t n = 0;
    wifi_scan_config_t sc = { .show_hidden = false };
    if (esp_wifi_scan_start(&sc, true) == ESP_OK) {
        esp_wifi_scan_get_ap_num(&n);
    }
    if (n > 16) {
        n = 16;
    }

    wifi_ap_record_t *recs = NULL;
    if (n) {
        recs = calloc(n, sizeof(*recs));
        if (recs) {
            esp_wifi_scan_get_ap_records(&n, recs);
        } else {
            n = 0;
        }
    }

    httpd_resp_set_type(req, "text/html");
    httpd_resp_sendstr_chunk(req,
        "<!doctype html><meta name=viewport content=width=device-width,initial-scale=1>"
        "<title>44net tester setup</title>"
        "<style>body{font-family:system-ui,sans-serif;margin:2rem;max-width:32rem}"
        "select,input{width:100%;padding:.5rem;margin:.3rem 0 1rem;box-sizing:border-box}"
        "button{padding:.6rem 1.2rem}</style>"
        "<h2>44net tester</h2>"
        "<form method=POST action=/save>"
        "<label>Network</label><select name=ssid>");

    for (int i = 0; i < n; i++) {
        char row[160];
        snprintf(row, sizeof(row), "<option value=\"%s\">%s (%d dBm)</option>",
                 (char *)recs[i].ssid, (char *)recs[i].ssid, recs[i].rssi);
        httpd_resp_sendstr_chunk(req, row);
    }
    free(recs);

    httpd_resp_sendstr_chunk(req,
        "</select>"
        "<label>or type it (hidden or out of range)</label>"
        "<input name=ssid_manual placeholder=SSID>"
        "<label>Passphrase</label><input name=pass type=password>"
        "<button type=submit>Save and reboot</button></form>");
    httpd_resp_sendstr_chunk(req, NULL);
    return ESP_OK;
}

static esp_err_t save_post(httpd_req_t *req)
{
    char body[320];
    int want = req->content_len;
    if (want > (int)sizeof(body) - 1) {
        want = sizeof(body) - 1;
    }
    int got = httpd_req_recv(req, body, want);
    if (got <= 0) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    body[got] = 0;

    netcfg_creds_t c = { 0 };
    char manual[NETCFG_SSID_LEN] = { 0 };
    form_field(body, "ssid", c.ssid, sizeof(c.ssid));
    form_field(body, "ssid_manual", manual, sizeof(manual));
    form_field(body, "pass", c.pass, sizeof(c.pass));

    /* free text wins when supplied: a scan is only a snapshot */
    if (manual[0]) {
        strlcpy(c.ssid, manual, sizeof(c.ssid));
    }

    if (!c.ssid[0]) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_sendstr(req, "no SSID given");
        return ESP_OK;
    }
    if (netcfg_save(&c) != ESP_OK) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "saved credentials for '%s'", c.ssid);
    httpd_resp_set_type(req, "text/html");
    httpd_resp_sendstr(req, "<h3>Saved. Rebooting into station mode.</h3>");
    s_saved = true;
    return ESP_OK;
}

/* Unknown paths - including the OS connectivity-probe URLs - redirect to the
   form, which is what makes the device register as a captive portal.
   Registered as a wildcard URI rather than relying on the 404 error hook:
   the hook fires too late for iOS, which retries /hotspot-detect.html and
   gives up rather than opening the portal sheet. */
static esp_err_t catchall_get(httpd_req_t *req)
{
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://" PORTAL_IP "/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t redirect_handler(httpd_req_t *req, httpd_err_code_t err)
{
    return catchall_get(req);
}

bool portal_run(int timeout_s)
{
    s_saved = false;
    ESP_ERROR_CHECK(netcfg_ap_start(CONFIG_TESTER_AP_SSID));

    s_dns_run = true;
    xTaskCreate(dns_task, "portal_dns", 3072, NULL, 5, NULL);

    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.max_uri_handlers = 8;
    cfg.lru_purge_enable = true;
    cfg.uri_match_fn     = httpd_uri_match_wildcard;
    if (httpd_start(&s_httpd, &cfg) != ESP_OK) {
        ESP_LOGE(TAG, "portal httpd failed to start");
        s_dns_run = false;
        return false;
    }

    /* Order matters: wildcard matching tries handlers in registration order,
       so the real pages must be registered before the catch-all. */
    httpd_uri_t u_root = { .uri = "/",     .method = HTTP_GET,  .handler = root_get };
    httpd_uri_t u_save = { .uri = "/save", .method = HTTP_POST, .handler = save_post };
    httpd_uri_t u_any  = { .uri = "/*",    .method = HTTP_GET,  .handler = catchall_get };
    httpd_register_uri_handler(s_httpd, &u_root);
    httpd_register_uri_handler(s_httpd, &u_save);
    httpd_register_uri_handler(s_httpd, &u_any);
    httpd_register_err_handler(s_httpd, HTTPD_404_NOT_FOUND, redirect_handler);

    ESP_LOGI(TAG, "portal open for %d s on http://%s/", timeout_s, PORTAL_IP);
    for (int i = 0; i < timeout_s * 10 && !s_saved; i++) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    vTaskDelay(pdMS_TO_TICKS(500));   /* let the final response flush */
    s_dns_run = false;
    httpd_stop(s_httpd);
    s_httpd = NULL;
    return s_saved;
}
