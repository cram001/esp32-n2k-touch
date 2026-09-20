idf_component_register(
    SRCS
        "main.cpp"
        "app_settings.cpp"
        "smartshunt_ble.cpp"
        "instrument_data.cpp"
        "n2k_bridge.cpp"
        "ota.cpp"
        "wifi_service.cpp"
        "ui.cpp"
    INCLUDE_DIRS "."
    REQUIRES nvs_flash bt mbedtls app_update esp_https_ota esp_http_client esp_wifi esp_netif esp_event
)

idf_component_get_property(LVGL_LIB lvgl__lvgl COMPONENT_LIB)
target_compile_options(
    ${LVGL_LIB}
    PRIVATE
        -Wno-error=attributes
        -DLV_LVGL_H_INCLUDE_SIMPLE
)
