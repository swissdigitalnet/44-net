#include <string.h>
#include "netcfg.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_system.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

static const char *TAG = "netcfg";
static const char *NVS_NS = "tester";

#define BIT_CONNECTED  BIT0
#define BIT_FAILED     BIT1

static EventGroupHandle_t s_events;
static esp_netif_t *s_sta_netif;
static esp_netif_t *s_ap_netif;
static bool s_ap_started;
static int  s_retries_left;
static char s_ip[16] = "0.0.0.0";
static char s_gw[16] = "0.0.0.0";
static uint32_t s_ip_raw, s_mask_raw;

static void on_wifi(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        strcpy(s_ip, "0.0.0.0");
        strcpy(s_gw, "0.0.0.0");
        if (s_retries_left > 0) {
            s_retries_left--;
            ESP_LOGW(TAG, "join failed, %d attempt(s) left", s_retries_left);
            esp_wifi_connect();
        } else {
            xEventGroupSetBits(s_events, BIT_FAILED);
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *e = (ip_event_got_ip_t *)data;
        snprintf(s_ip, sizeof(s_ip), IPSTR, IP2STR(&e->ip_info.ip));
        snprintf(s_gw, sizeof(s_gw), IPSTR, IP2STR(&e->ip_info.gw));
        s_ip_raw   = e->ip_info.ip.addr;
        s_mask_raw = e->ip_info.netmask.addr;
        ESP_LOGI(TAG, "got %s", s_ip);
        xEventGroupSetBits(s_events, BIT_CONNECTED);
    }
}

esp_err_t netcfg_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    s_sta_netif = esp_netif_create_default_wifi_sta();
    s_ap_netif  = esp_netif_create_default_wifi_ap();
    s_events    = xEventGroupCreate();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_wifi, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_wifi, NULL, NULL));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    return ESP_OK;
}

esp_err_t netcfg_load(netcfg_creds_t *out)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READONLY, &h);
    if (err != ESP_OK) return err;

    size_t n = NETCFG_SSID_LEN;
    err = nvs_get_str(h, "ssid", out->ssid, &n);
    if (err == ESP_OK) {
        n = NETCFG_PASS_LEN;
        err = nvs_get_str(h, "pass", out->pass, &n);
        if (err == ESP_ERR_NVS_NOT_FOUND) { out->pass[0] = '\0'; err = ESP_OK; }
    }
    nvs_close(h);
    return err;
}

esp_err_t netcfg_save(const netcfg_creds_t *in)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_str(h, "ssid", in->ssid);
    if (err == ESP_OK) err = nvs_set_str(h, "pass", in->pass);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

esp_err_t netcfg_erase(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    nvs_erase_all(h);
    err = nvs_commit(h);
    nvs_close(h);
    return err;
}

bool netcfg_sta_connect(const netcfg_creds_t *c, int max_retry, int per_try_ms)
{
    wifi_config_t wc = { 0 };
    strlcpy((char *)wc.sta.ssid, c->ssid, sizeof(wc.sta.ssid));
    strlcpy((char *)wc.sta.password, c->pass, sizeof(wc.sta.password));

    s_retries_left = max_retry;
    xEventGroupClearBits(s_events, BIT_CONNECTED | BIT_FAILED);

    ESP_ERROR_CHECK(esp_wifi_set_mode(s_ap_started ? WIFI_MODE_APSTA : WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wc));
    ESP_ERROR_CHECK(esp_wifi_start());

    TickType_t budget = pdMS_TO_TICKS((int64_t)per_try_ms * (max_retry + 1));
    EventBits_t bits = xEventGroupWaitBits(s_events, BIT_CONNECTED | BIT_FAILED,
                                           pdFALSE, pdFALSE, budget);
    return (bits & BIT_CONNECTED) != 0;
}

esp_err_t netcfg_ap_start(const char *ssid)
{
    wifi_config_t wc = { 0 };
    strlcpy((char *)wc.ap.ssid, ssid, sizeof(wc.ap.ssid));
    wc.ap.ssid_len       = strlen(ssid);
    wc.ap.channel        = 1;
    wc.ap.max_connection = 4;
    wc.ap.authmode       = WIFI_AUTH_OPEN;   /* deliberate: recovery windows only */

    /* APSTA, not AP: scanning is a station-interface operation and the portal
       lists networks with their RSSI. */
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wc));
    ESP_ERROR_CHECK(esp_wifi_start());
    s_ap_started = true;
    ESP_LOGI(TAG, "portal AP up: %s (open)", ssid);
    return ESP_OK;
}

#define BTN_POLL_MS 100

static void reset_button_task(void *arg)
{
    const int hold_ms = (int)(intptr_t)arg;
    int held = 0;

    for (;;) {
        if (gpio_get_level(CONFIG_TESTER_BOOT_GPIO) == 0) {
            held += BTN_POLL_MS;
            if (held == 1000 || held == 2000) {
                ESP_LOGW(TAG, "button held %d ms (release before %d ms to cancel)",
                         held, hold_ms);
            }
            if (held >= hold_ms) {
                ESP_LOGW(TAG, "button held %d ms - erasing credentials and rebooting",
                         held);
                netcfg_erase();
                vTaskDelay(pdMS_TO_TICKS(200));
                esp_restart();
            }
        } else {
            held = 0;   /* must be a continuous hold, not an accumulation */
        }
        vTaskDelay(pdMS_TO_TICKS(BTN_POLL_MS));
    }
}

void netcfg_watch_reset_button(int hold_ms)
{
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << CONFIG_TESTER_BOOT_GPIO,
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    if (gpio_config(&io) != ESP_OK) {
        ESP_LOGE(TAG, "cannot configure GPIO%d - reset button disabled",
                 CONFIG_TESTER_BOOT_GPIO);
        return;
    }
    xTaskCreate(reset_button_task, "reset_btn", 3072,
                (void *)(intptr_t)hold_ms, 4, NULL);
    ESP_LOGI(TAG, "reset button on GPIO%d, hold %d ms to forget the network",
             CONFIG_TESTER_BOOT_GPIO, hold_ms);
}

const char *netcfg_ip_str(void) { return s_ip; }
const char *netcfg_gw_str(void) { return s_gw; }

bool netcfg_is_local(uint32_t addr)
{
    if (!s_mask_raw) {
        return false;
    }
    return (addr & s_mask_raw) == (s_ip_raw & s_mask_raw);
}

int netcfg_rssi(void)
{
    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) return ap.rssi;
    return 0;
}
