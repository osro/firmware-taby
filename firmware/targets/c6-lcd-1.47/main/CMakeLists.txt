set(TABY_SHARED "${CMAKE_CURRENT_LIST_DIR}/../../../main")
idf_component_register(
    SRCS "app_main.c" "c6_board.c" "c6_codec.c"
         "${TABY_SHARED}/taby_animation_assets.c"
         "${TABY_SHARED}/taby_state_machine.c"
         "${TABY_SHARED}/taby_transport_protocol.c"
         "${TABY_SHARED}/taby_line_reader.c"
    INCLUDE_DIRS "." "${TABY_SHARED}"
    REQUIRES esp_lcd_jd9853 esp_lcd driver esp_partition esp_timer esp_hw_support esp_system)
if(NOT EXISTS "${CMAKE_CURRENT_LIST_DIR}/generated/c6_catalog.h")
    message(FATAL_ERROR "Run .venv/bin/python tools/c6_assets.py from repository root first")
endif()
