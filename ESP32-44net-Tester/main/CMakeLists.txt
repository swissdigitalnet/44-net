idf_component_register(
    SRCS "main.c" "netcfg.c" "portal.c" "tester.c"
    INCLUDE_DIRS "."
    REQUIRES nvs_flash esp_wifi esp_netif esp_http_server app_update esp_app_format esp_driver_gpio
)
