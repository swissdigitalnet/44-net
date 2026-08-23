/* 44net reachability tester - passive target.
 *
 * It answers; it never probes. Its job is to be the "something on the 44net
 * segment to aim at" that docs/test-procedure.md requires, and to report what
 * actually arrived - in particular the source address the caller presented,
 * which is the thing that makes an asymmetric or translated path visible
 * instead of silent.
 */

#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "netcfg.h"
#include "portal.h"
#include "tester.h"

static const char *TAG = "main";

#define STA_PER_TRY_MS  10000
#define OTA_SETTLE_S    30

static void reboot(const char *why)
{
    ESP_LOGW(TAG, "restarting: %s", why);
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
}

void app_main(void)
{
    ESP_ERROR_CHECK(netcfg_init());

    /* Read the button before anything else touches the pin. */
    bool forced = netcfg_boot_button_pressed();
    if (forced) {
        ESP_LOGW(TAG, "BOOT held - forcing the provisioning portal");
    }

    netcfg_creds_t creds = { 0 };
    bool have_creds = (netcfg_load(&creds) == ESP_OK) && creds.ssid[0];

    if (forced || !have_creds) {
        ESP_LOGI(TAG, forced ? "portal: requested" : "portal: no stored credentials");
        portal_run(CONFIG_TESTER_PORTAL_TIMEOUT_S);
        /* Either way we reboot: on success to come up cleanly in station mode,
           on timeout to retry whatever is stored. Never linger as an AP. */
        reboot("leaving portal");
    }

    ESP_LOGI(TAG, "joining '%s'", creds.ssid);
    if (!netcfg_sta_connect(&creds, CONFIG_TESTER_STA_MAX_RETRY, STA_PER_TRY_MS)) {
        /* Bounded portal window, then reboot and try the stored credentials
           again. A transient AP outage must not park the device in AP mode. */
        ESP_LOGW(TAG, "join failed - opening the portal for %d s",
                 CONFIG_TESTER_PORTAL_TIMEOUT_S);
        portal_run(CONFIG_TESTER_PORTAL_TIMEOUT_S);
        reboot("join failed");
    }

    ESP_LOGI(TAG, "connected, ip=%s rssi=%d", netcfg_ip_str(), netcfg_rssi());

    if (tester_start() != ESP_OK) {
        /* Listener could not bind. Do not mark the image valid - let the
           bootloader roll back rather than leave a deaf device at a remote
           site. */
        reboot("listener failed to start");
    }

    /* Only now is the image a candidate for validation, and only after it has
       held for a while. If this firmware cannot rejoin or cannot serve, it
       never gets here and the previous image is restored on the next boot. */
    tester_confirm_after(OTA_SETTLE_S);

    /* Once connected we reconnect with backoff and never fall back to the
       portal: a brief association loss during an AP reload must not cost us
       the device. */
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}
