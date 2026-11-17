configure_file("${CMAKE_CURRENT_LIST_DIR}/../../../components/taby_character/taby_voice.cpp"
    "${CMAKE_CURRENT_BINARY_DIR}/c6_voice_upstream/taby_voice.cpp" COPYONLY)
configure_file("${CMAKE_CURRENT_LIST_DIR}/c6_voice_compat/taby_voice.hpp"
    "${CMAKE_CURRENT_BINARY_DIR}/c6_voice_upstream/taby_voice.hpp" COPYONLY)
set(TABY_SHARED "${CMAKE_CURRENT_LIST_DIR}/../../../main")
idf_component_register(
    SRCS "app_main.c" "c6_board.c" "c6_codec.c" "c6_file.c" "c6_sd.c" "c6_stream.c" "c6_geometry.c"
         "c6_preferences.c" "c6_preferences_nvs.c"
         "c6_ui.c" "c6_ui_model.c"
         "c6_network.c" "c6_ble.c" "c6_ble_v2.c" "c6_net_helpers.c"
         "c6_voice.cpp" "${CMAKE_CURRENT_BINARY_DIR}/c6_voice_upstream/taby_voice.cpp"
         "${TABY_SHARED}/taby_animation_assets.c"
         "${TABY_SHARED}/taby_state_machine.c"
         "${TABY_SHARED}/taby_transport_protocol.c"
         "${TABY_SHARED}/taby_line_reader.c"
         "${TABY_SHARED}/taby_eye_motion.c"
         "${TABY_SHARED}/taby_reusable_preview.c"
         "${TABY_SHARED}/generated/taby_reusable_icons.c"
    INCLUDE_DIRS "." "${TABY_SHARED}"
    REQUIRES esp_lcd_jd9853 esp_lcd driver esp_partition esp_timer esp_hw_support esp_system fatfs sdmmc esp_adc nvs_flash lvgl esp_wifi esp_netif esp_event esp_http_server json bt mdns)
if(NOT EXISTS "${CMAKE_CURRENT_LIST_DIR}/generated/c6_catalog.h")
    message(FATAL_ERROR "Run .venv/bin/python tools/c6_assets.py from repository root first")
endif()

if(C6_SD_TRACE)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE C6_SD_TRACE=1)
endif()
