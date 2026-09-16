idf_component_register(
    SRCS
        "main.cpp"
        "app_settings.cpp"
        "ui.cpp"
    INCLUDE_DIRS "."
    REQUIRES nvs_flash
)

idf_component_get_property(LVGL_LIB lvgl__lvgl COMPONENT_LIB)
target_compile_options(
    ${LVGL_LIB}
    PRIVATE
        -Wno-error=attributes
        -DLV_LVGL_H_INCLUDE_SIMPLE
)
