#pragma once
#include <stdbool.h>
#include "esp_err.h"
#include "esp_netif.h"

#define NETCFG_SSID_LEN 33
#define NETCFG_PASS_LEN 65

typedef struct {
    char ssid[NETCFG_SSID_LEN];
    char pass[NETCFG_PASS_LEN];
} netcfg_creds_t;

/* One-time bring-up of nvs, netif, the default event loop and esp_wifi. */
esp_err_t netcfg_init(void);

esp_err_t netcfg_load(netcfg_creds_t *out);   /* ESP_ERR_NVS_NOT_FOUND if unset */
esp_err_t netcfg_save(const netcfg_creds_t *in);
esp_err_t netcfg_erase(void);

/* Blocks until connected or every attempt is exhausted. */
bool netcfg_sta_connect(const netcfg_creds_t *c, int max_retry, int per_try_ms);

/* APSTA so the portal can scan while serving. */
esp_err_t netcfg_ap_start(const char *ssid);

/* Watches the button throughout normal operation: held for hold_ms it erases
   the stored credentials and reboots, which brings the device up in the
   provisioning portal.

   Deliberately NOT read at reset. The ROM bootloader samples GPIO0 there and
   enters serial download mode when it is low, so a button checked at reset can
   never be seen by the application at all. */
void netcfg_watch_reset_button(int hold_ms);

const char *netcfg_ip_str(void);   /* "0.0.0.0" until DHCP completes */
const char *netcfg_gw_str(void);   /* the DHCP-supplied gateway */

/* true when addr (network byte order) is on the same subnet as this device.
   Used to tell a caller on the local segment from one arriving via a tunnel. */
bool netcfg_is_local(uint32_t addr);
int netcfg_rssi(void);
