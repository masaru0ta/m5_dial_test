idf_component_register(
    SRCS "main.cpp"
    INCLUDE_DIRS "."
    REQUIRES driver nvs_flash esp_wifi esp_http_server app_update esp_netif LovyanGFX
)
