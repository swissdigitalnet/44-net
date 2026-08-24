#include <string.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include "tester.h"
#include "netcfg.h"
#include "esp_http_server.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

static const char *TAG = "tester";

#ifndef TESTER_VERSION
#define TESTER_VERSION "unknown"
#endif

/* ---------- arrivals ring ----------
   Fixed size and deduplicated by source: a 44net allocation is scanned
   continuously, so an unbounded log is a memory-exhaustion bug. Headers only,
   never payload - payload is attacker-controlled and has no business here. */

#define ARRIVALS_MAX 16

typedef struct {
    bool     used;
    uint8_t  proto;        /* IPPROTO_TCP / IPPROTO_UDP */
    uint32_t src_ip;       /* network byte order */
    uint16_t src_port;
    uint16_t dst_port;
    uint32_t count;
    int64_t  last_us;
} arrival_t;

static arrival_t s_arrivals[ARRIVALS_MAX];
static SemaphoreHandle_t s_lock;
static uint32_t s_next_slot;

static void arrival_record(uint8_t proto, uint32_t src_ip, uint16_t src_port, uint16_t dst_port)
{
    if (!s_lock) {
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);

    for (int i = 0; i < ARRIVALS_MAX; i++) {
        arrival_t *a = &s_arrivals[i];
        if (a->used && a->proto == proto && a->src_ip == src_ip && a->dst_port == dst_port) {
            a->count++;
            a->src_port = src_port;
            a->last_us  = esp_timer_get_time();
            xSemaphoreGive(s_lock);
            return;
        }
    }

    arrival_t *a = &s_arrivals[s_next_slot % ARRIVALS_MAX];
    s_next_slot++;
    a->used     = true;
    a->proto    = proto;
    a->src_ip   = src_ip;
    a->src_port = src_port;
    a->dst_port = dst_port;
    a->count    = 1;
    a->last_us  = esp_timer_get_time();

    xSemaphoreGive(s_lock);
}

/* ---------- peer address of an HTTP request ---------- */

static bool peer_of(httpd_req_t *req, uint32_t *ip, uint16_t *port)
{
    int fd = httpd_req_to_sockfd(req);
    if (fd < 0) {
        return false;
    }
    struct sockaddr_in6 sa;
    socklen_t len = sizeof(sa);
    if (getpeername(fd, (struct sockaddr *)&sa, &len) != 0) {
        return false;
    }
    if (sa.sin6_family == AF_INET) {
        struct sockaddr_in *s4 = (struct sockaddr_in *)&sa;
        *ip   = s4->sin_addr.s_addr;
        *port = ntohs(s4->sin_port);
    } else {
        /* IPv4-mapped IPv6: the v4 address sits in the last four bytes */
        memcpy(ip, ((uint8_t *)&sa.sin6_addr) + 12, 4);
        *port = ntohs(sa.sin6_port);
    }
    return true;
}

static const char *ota_state_str(void)
{
    const esp_partition_t *run = esp_ota_get_running_partition();
    esp_ota_img_states_t st;
    if (!run || esp_ota_get_state_partition(run, &st) != ESP_OK) {
        return "unknown";
    }
    switch (st) {
        case ESP_OTA_IMG_VALID:           return "valid";
        case ESP_OTA_IMG_PENDING_VERIFY:  return "pending";
        case ESP_OTA_IMG_NEW:             return "new";
        case ESP_OTA_IMG_INVALID:         return "invalid";
        case ESP_OTA_IMG_ABORTED:         return "aborted";
        default:                          return "undefined";
    }
}

/* ---------- verdicts, read off the arrivals ring ----------
   Still passive: this interprets what already arrived, it does not probe. */

static bool ip_is_44net(uint32_t nip)
{
    uint32_t h = ntohl(nip);
    return (h & 0xFF800000u) == 0x2C000000u ||    /* 44.0.0.0/9   */
           (h & 0xFFC00000u) == 0x2C800000u;      /* 44.128.0.0/10 */
}

static bool ip_is_private(uint32_t nip)
{
    uint32_t h = ntohl(nip);
    return (h & 0xFF000000u) == 0x0A000000u ||    /* 10/8     */
           (h & 0xFFF00000u) == 0xAC100000u ||    /* 172.16/12 */
           (h & 0xFFFF0000u) == 0xC0A80000u ||    /* 192.168/16 */
           (h & 0xFFC00000u) == 0x64400000u;      /* 100.64/10 */
}

static int render_verdicts(char *out, size_t len)
{
    bool http_seen = false, udp_seen = false, from_44 = false, from_public = false;
    int64_t now = esp_timer_get_time(), newest = 0;
    char newest_src[24] = "-";

    if (s_lock) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        for (int i = 0; i < ARRIVALS_MAX; i++) {
            arrival_t *a = &s_arrivals[i];
            if (!a->used) continue;
            if (a->proto == IPPROTO_TCP && a->dst_port == 80)               http_seen = true;
            if (a->proto == IPPROTO_UDP && a->dst_port == CONFIG_TESTER_UDP_PORT) udp_seen = true;
            if (ip_is_44net(a->src_ip))                                     from_44 = true;
            if (!ip_is_private(a->src_ip))                                  from_public = true;
            if (a->last_us > newest) {
                struct in_addr in = { .s_addr = a->src_ip };
                newest = a->last_us;
                strlcpy(newest_src, inet_ntoa(in), sizeof(newest_src));
            }
        }
        xSemaphoreGive(s_lock);
    }

    return snprintf(out, len,
        "observed:\n"
        "  %s reached on tcp/80\n"
        "  %s probe on udp/%d\n"
        "  %s reached from 44net space\n"
        "  %s caller had a public address (no NAT in the way)\n"
        "  last caller %s, %llus ago\n",
        http_seen   ? "yes" : "NO ",
        udp_seen    ? "yes" : "NO ", CONFIG_TESTER_UDP_PORT,
        from_44     ? "yes" : "NO ",
        from_public ? "yes" : "NO ",
        newest_src,
        (unsigned long long)(newest ? (now - newest) / 1000000 : 0));
}

/* ---------- active checks ----------
   The segment side of docs/test-procedure.md: tests 2, 7 and 8 are things only
   a host ON the segment can attempt. A TCP connect tells the three cases apart:
   open (reached and accepted), refused (reached, declined - nothing filtered),
   blocked (no answer at all - something dropped it). */

static const char *probe_tcp(const char *ip, int port, int timeout_ms)
{
    if (!ip || !ip[0] || strcmp(ip, "0.0.0.0") == 0) return "skip";

    int s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s < 0) return "error";

    struct timeval tv = { .tv_sec = timeout_ms / 1000,
                          .tv_usec = (timeout_ms % 1000) * 1000 };
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    struct sockaddr_in a = { .sin_family = AF_INET, .sin_port = htons(port) };
    inet_pton(AF_INET, ip, &a.sin_addr);

    errno = 0;
    int r = connect(s, (struct sockaddr *)&a, sizeof(a));
    int e = errno;
    close(s);

    if (r == 0)              return "open";
    if (e == ECONNREFUSED)   return "refused";
    return "blocked";
}

static int render_checks(char *out, size_t len)
{
    const char *gw = netcfg_gw_str();
    int n = snprintf(out, len, "checks (from this device, just now):\n");

    n += snprintf(out + n, len - n, "  %-8s  gateway %s:53   want open\n",
                  probe_tcp(gw, 53, 1500), gw);
    n += snprintf(out + n, len - n, "  %-8s  gateway %s:22   want blocked on a DMZ\n",
                  probe_tcp(gw, 22, 1500), gw);
#if defined(CONFIG_TESTER_CHECK_44NET)
    n += snprintf(out + n, len - n, "  %-8s  44net %s   want open\n",
                  probe_tcp(CONFIG_TESTER_CHECK_44NET, 80, 2500),
                  CONFIG_TESTER_CHECK_44NET ":80");
#endif
#if defined(CONFIG_TESTER_CHECK_INTERNAL)
    n += snprintf(out + n, len - n, "  %-8s  internal %s   want blocked on a DMZ\n",
                  probe_tcp(CONFIG_TESTER_CHECK_INTERNAL, 80, 1500),
                  CONFIG_TESTER_CHECK_INTERNAL ":80");
#endif
#if defined(CONFIG_TESTER_CHECK_INTERNET)
    n += snprintf(out + n, len - n, "  %-8s  internet %s   depends on your egress policy\n",
                  probe_tcp(CONFIG_TESTER_CHECK_INTERNET, 53, 2500),
                  CONFIG_TESTER_CHECK_INTERNET ":53");
#endif
    n += snprintf(out + n, len - n,
                  "\n  open = reached and accepted; refused = reached and declined,\n"
                  "  nothing filtered it; blocked = no answer, something dropped it.\n"
                  "  \"want\" assumes an isolated 44net segment. On an ordinary LAN\n"
                  "  several of these read open, and that is correct there.\n");
    return n;
}

static int render_status(char *out, size_t len)
{
    int64_t now = esp_timer_get_time();
    int n = snprintf(out, len,
                     "id=esp32-44net-tester\n"
                     "version=%s\n"
                     "ota=%s\n"
                     "uptime=%llus\n"
                     "ip=%s\n"
                     "rssi=%d\n"
                     "heap=%u\n"
                     "udp_port=%d\n"
                     "\narrivals (last %d):\n",
                     TESTER_VERSION, ota_state_str(),
                     (unsigned long long)(now / 1000000),
                     netcfg_ip_str(), netcfg_rssi(),
                     (unsigned)esp_get_free_heap_size(),
                     CONFIG_TESTER_UDP_PORT, ARRIVALS_MAX);

    if (!s_lock) {
        return n;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    for (int i = 0; i < ARRIVALS_MAX && n < (int)len; i++) {
        arrival_t *a = &s_arrivals[i];
        if (!a->used) {
            continue;
        }
        struct in_addr in = { .s_addr = a->src_ip };
        n += snprintf(out + n, len - n, "  %-3s %s:%u -> :%u x%u %llus ago\n",
                      a->proto == IPPROTO_UDP ? "udp" : "tcp",
                      inet_ntoa(in),
                      (unsigned)a->src_port, (unsigned)a->dst_port,
                      (unsigned)a->count,
                      (unsigned long long)((now - a->last_us) / 1000000));
    }
    xSemaphoreGive(s_lock);
    return n;
}

/* ---------- handlers ---------- */

static esp_err_t status_get(httpd_req_t *req)
{
    uint32_t ip = 0;
    uint16_t port = 0;
    char you[32] = "unknown";
    if (peer_of(req, &ip, &port)) {
        struct in_addr in = { .s_addr = ip };
        snprintf(you, sizeof(you), "%s:%u", inet_ntoa(in), (unsigned)port);
        arrival_record(IPPROTO_TCP, ip, port, 80);
    }

    char *buf = malloc(2048);
    if (!buf) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    int n = snprintf(buf, 2048, "you=%s\n", you);
    n += render_status(buf + n, 2048 - n);
    n += snprintf(buf + n, 2048 - n, "\n");
    n += render_verdicts(buf + n, 2048 - n);

    httpd_resp_set_type(req, "text/plain");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_send(req, buf, n);
    free(buf);
    return ESP_OK;
}

static esp_err_t ui_get(httpd_req_t *req)
{
    uint32_t ip = 0;
    uint16_t port = 0;
    char you[32] = "unknown";
    if (peer_of(req, &ip, &port)) {
        struct in_addr in = { .s_addr = ip };
        snprintf(you, sizeof(you), "%s:%u", inet_ntoa(in), (unsigned)port);
        arrival_record(IPPROTO_TCP, ip, port, 80);
    }

    char *buf = malloc(2048);
    if (!buf) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    int n = snprintf(buf, 2048, "you=%s\n", you);
    n += render_status(buf + n, 2048 - n);
    n += snprintf(buf + n, 2048 - n, "\n");
    n += render_verdicts(buf + n, 2048 - n);

    /* Phone-shaped on purpose: this is read standing next to the thing, on a
       handset joined to the same segment. Dark, large type, no horizontal
       scroll, and the one field that matters called out at the top. */
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_sendstr_chunk(req,
        "<!doctype html><meta charset=utf-8>"
        "<meta name=viewport content='width=device-width,initial-scale=1'>"
        "<meta http-equiv=refresh content=10>"
        "<title>44net tester</title>"
        "<style>"
        ":root{color-scheme:dark light}"
        "body{font-family:ui-monospace,SFMono-Regular,Menlo,monospace;"
        "margin:0;padding:1.2rem;background:#111;color:#e8e8e8;font-size:15px}"
        "h1{font-size:1.1rem;margin:0 0 .2rem;color:#8ecbff}"
        ".you{background:#1c2a38;border-left:3px solid #8ecbff;"
        "padding:.6rem .8rem;margin:.8rem 0;word-break:break-all}"
        ".you b{color:#8ecbff}"
        "pre{margin:0;white-space:pre-wrap;word-break:break-word;line-height:1.5}"
        "footer{margin-top:1rem;color:#777;font-size:.8rem}"
        "a.btn{display:inline-block;margin-top:1rem;padding:.7rem 1.1rem;"
        "background:#1c2a38;color:#8ecbff;text-decoration:none;border-radius:6px}"
        "</style>"
        "<h1>44net tester</h1>");

    /* The caller's address, first and unmissable - it is the whole point. */
    httpd_resp_sendstr_chunk(req, "<div class=you><b>you are</b><br>");
    httpd_resp_sendstr_chunk(req, you);
    httpd_resp_sendstr_chunk(req, "</div><pre>");

    /* buf still begins with the you= line; skip it, it is shown above. */
    const char *rest = strchr(buf, '\n');
    httpd_resp_sendstr_chunk(req, rest ? rest + 1 : buf);
    httpd_resp_sendstr_chunk(req,
        "</pre><a class=btn href=/run>run checks</a>"
        "<footer>refreshes every 10 s &middot; plain text at /</footer>");
    httpd_resp_sendstr_chunk(req, NULL);
    free(buf);
    return ESP_OK;
}

/* Runs the checks when asked, never on a timer: each one is a real connection
   attempt and this device is meant to sit still and answer. */
static esp_err_t run_get(httpd_req_t *req)
{
    char *buf = malloc(1024);
    if (!buf) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    int n = render_checks(buf, 1024);

    const char *accept = NULL;
    char hdr[64] = { 0 };
    if (httpd_req_get_hdr_value_str(req, "Accept", hdr, sizeof(hdr)) == ESP_OK) {
        accept = hdr;
    }

    if (accept && strstr(accept, "text/html")) {
        httpd_resp_set_type(req, "text/html");
        httpd_resp_set_hdr(req, "Cache-Control", "no-store");
        httpd_resp_sendstr_chunk(req,
            "<!doctype html><meta charset=utf-8>"
            "<meta name=viewport content='width=device-width,initial-scale=1'>"
            "<title>checks</title><style>"
            "body{font-family:ui-monospace,SFMono-Regular,Menlo,monospace;margin:0;"
            "padding:1.2rem;background:#111;color:#e8e8e8;font-size:15px}"
            "h1{font-size:1.1rem;margin:0 0 .8rem;color:#8ecbff}"
            "pre{white-space:pre-wrap;word-break:break-word;line-height:1.5}"
            "a{display:inline-block;margin-top:1rem;padding:.6rem 1rem;"
            "background:#1c2a38;color:#8ecbff;text-decoration:none;border-radius:6px}"
            "</style><h1>checks</h1><pre>");
        httpd_resp_sendstr_chunk(req, buf);
        httpd_resp_sendstr_chunk(req, "</pre><a href=/run>run again</a> <a href=/ui>back</a>");
        httpd_resp_sendstr_chunk(req, NULL);
    } else {
        httpd_resp_set_type(req, "text/plain");
        httpd_resp_set_hdr(req, "Cache-Control", "no-store");
        httpd_resp_send(req, buf, n);
    }
    free(buf);
    return ESP_OK;
}

#if CONFIG_TESTER_OTA_ENABLE
static esp_err_t ota_post(httpd_req_t *req)
{
    /* The firewall rule scoped to your update host is the real control here.
       This token only guards against a mistake: it crosses plaintext HTTP. */
    char hdr[128] = { 0 };
    char want[128];
    snprintf(want, sizeof(want), "Bearer %s", CONFIG_TESTER_OTA_TOKEN);
    if (httpd_req_get_hdr_value_str(req, "Authorization", hdr, sizeof(hdr)) != ESP_OK ||
        strcmp(hdr, want) != 0) {
        httpd_resp_set_status(req, "401 Unauthorized");
        httpd_resp_sendstr(req, "bad token\n");
        return ESP_OK;
    }

    const esp_partition_t *target = esp_ota_get_next_update_partition(NULL);
    if (!target) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "ota -> %s, %u bytes", target->label, (unsigned)req->content_len);

    esp_ota_handle_t h;
    if (esp_ota_begin(target, OTA_WITH_SEQUENTIAL_WRITES, &h) != ESP_OK) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    char *buf = malloc(4096);
    if (!buf) {
        esp_ota_abort(h);
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    int left = req->content_len;
    esp_err_t err = ESP_OK;
    while (left > 0) {
        int got = httpd_req_recv(req, buf, left > 4096 ? 4096 : left);
        if (got <= 0) {
            err = ESP_FAIL;
            break;
        }
        err = esp_ota_write(h, buf, got);
        if (err != ESP_OK) {
            break;
        }
        left -= got;
    }
    free(buf);

    if (err != ESP_OK) {
        esp_ota_abort(h);
        ESP_LOGE(TAG, "ota write failed");
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_sendstr(req, "write failed\n");
        return ESP_OK;
    }
    if (esp_ota_end(h) != ESP_OK || esp_ota_set_boot_partition(target) != ESP_OK) {
        ESP_LOGE(TAG, "ota finalise failed (bad image?)");
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_sendstr(req, "image rejected\n");
        return ESP_OK;
    }

    httpd_resp_sendstr(req, "ok, rebooting\n");
    ESP_LOGW(TAG, "rebooting into %s", target->label);
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    return ESP_OK;
}
#endif

/* ---------- UDP listener ----------
   Answers test 4 of docs/test-procedure.md, which sends a UDP probe to an
   allowed port. Without this the operator still needs tcpdump on the segment. */

static void udp_task(void *arg)
{
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        ESP_LOGE(TAG, "udp socket failed");
        vTaskDelete(NULL);
        return;
    }
    struct sockaddr_in me = {
        .sin_family      = AF_INET,
        .sin_port        = htons(CONFIG_TESTER_UDP_PORT),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    if (bind(sock, (struct sockaddr *)&me, sizeof(me)) < 0) {
        ESP_LOGE(TAG, "udp bind %d failed", CONFIG_TESTER_UDP_PORT);
        close(sock);
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "udp listener on %d", CONFIG_TESTER_UDP_PORT);

    uint8_t buf[256];
    for (;;) {
        struct sockaddr_in from;
        socklen_t fl = sizeof(from);
        int n = recvfrom(sock, buf, sizeof(buf), 0, (struct sockaddr *)&from, &fl);
        if (n < 0) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        /* headers only - the payload is discarded unread */
        arrival_record(IPPROTO_UDP, from.sin_addr.s_addr, ntohs(from.sin_port),
                       CONFIG_TESTER_UDP_PORT);
    }
}

/* ---------- rollback confirmation ---------- */

static void confirm_task(void *arg)
{
    int settle_s = (int)(intptr_t)arg;
    vTaskDelay(pdMS_TO_TICKS(settle_s * 1000));

    const esp_partition_t *run = esp_ota_get_running_partition();
    esp_ota_img_states_t st;
    if (run && esp_ota_get_state_partition(run, &st) == ESP_OK &&
        st == ESP_OTA_IMG_PENDING_VERIFY) {
        ESP_LOGI(TAG, "image held %d s with WiFi up - marking valid", settle_s);
        esp_ota_mark_app_valid_cancel_rollback();
    }
    vTaskDelete(NULL);
}

void tester_confirm_after(int settle_s)
{
    xTaskCreate(confirm_task, "ota_confirm", 3072, (void *)(intptr_t)settle_s, 4, NULL);
}

esp_err_t tester_start(void)
{
    s_lock = xSemaphoreCreateMutex();
    if (!s_lock) {
        return ESP_ERR_NO_MEM;
    }

    httpd_handle_t srv = NULL;
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.max_uri_handlers = 8;
    cfg.lru_purge_enable = true;
    cfg.recv_wait_timeout = 10;
    cfg.send_wait_timeout = 10;

    esp_err_t err = httpd_start(&srv, &cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "status httpd failed: %s", esp_err_to_name(err));
        return err;
    }

    httpd_uri_t u_root = { .uri = "/",   .method = HTTP_GET, .handler = status_get };
    httpd_uri_t u_ui   = { .uri = "/ui",  .method = HTTP_GET, .handler = ui_get };
    httpd_uri_t u_run  = { .uri = "/run", .method = HTTP_GET, .handler = run_get };
    httpd_register_uri_handler(srv, &u_root);
    httpd_register_uri_handler(srv, &u_ui);
    httpd_register_uri_handler(srv, &u_run);
#if CONFIG_TESTER_OTA_ENABLE
    httpd_uri_t u_ota  = { .uri = "/ota", .method = HTTP_POST, .handler = ota_post };
    httpd_register_uri_handler(srv, &u_ota);
#endif

    xTaskCreate(udp_task, "tester_udp", 3072, NULL, 5, NULL);
    ESP_LOGI(TAG, "status server up, version=%s", TESTER_VERSION);
    return ESP_OK;
}
