idf_component_register(
    SRCS
        "main.cpp"
        "app_settings.cpp"
        "smartshunt_ble.cpp"
        "instrument_data.cpp"
        "n2k_bridge.cpp"
        "n2k_input.cpp"
        "actisense_ascii.cpp"
        "third_party/nmea2000_esp32xx/NMEA2000_esp32xx.cpp"
        "wifi_service.cpp"
        "wifi_config.cpp"
        "ota.cpp"
        "ui.cpp"
    INCLUDE_DIRS "."
    REQUIRES nvs_flash bt mbedtls esp_wifi esp_netif esp_event driver esp_driver_twai esp_https_ota esp_http_client esp_http_server app_update bootloader_support
)

idf_component_get_property(LVGL_LIB lvgl__lvgl COMPONENT_LIB)
target_compile_options(
    ${LVGL_LIB}
    PRIVATE
        -Wno-error=attributes
        -DLV_LVGL_H_INCLUDE_SIMPLE
)
