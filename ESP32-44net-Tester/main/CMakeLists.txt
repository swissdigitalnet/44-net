# One entry per component actually providing a header we include:
#   nvs.h, nvs_flash.h      -> nvs_flash
#   esp_wifi.h              -> esp_wifi
#   esp_netif.h             -> esp_netif
#   esp_event.h             -> esp_event
#   esp_http_server.h       -> esp_http_server
#   esp_ota_ops.h           -> app_update
#   driver/gpio.h           -> esp_driver_gpio   (moved out of driver in IDF 6)
#   esp_timer.h             -> esp_timer
#   sys/socket.h, arpa/...  -> lwip
#   esp_system.h, esp_log.h, freertos/* come in via the common requirements
idf_component_register(
    SRCS "main.c" "netcfg.c" "portal.c" "tester.c"
    INCLUDE_DIRS "."
    REQUIRES nvs_flash esp_wifi esp_netif esp_event esp_http_server
             app_update esp_driver_gpio esp_timer lwip
)
