set(vendor "../vendor/esp-sdr")
idf_component_register(SRCS "main.c" "snapshot.c"
    "${vendor}/rx_gain_verify.c" "${vendor}/rx_recalibration.c" "${vendor}/rx_calibration_state.c"
    INCLUDE_DIRS "." "${vendor}"
    REQUIRES esp_driver_usb_serial_jtag esp_driver_tsens esp_hw_support esp_phy esp_wifi esp_timer heap esp_event)
target_link_options(${COMPONENT_LIB} INTERFACE
    "-T${CMAKE_CURRENT_LIST_DIR}/sram_guard.ld"
    "-Wl,--wrap=chip_v7_set_chan_ana" "-Wl,--wrap=set_rx_gain_table" "-Wl,--wrap=set_rx_gain_cal_dc")
execute_process(COMMAND git -C "$ENV{IDF_PATH}" rev-parse HEAD OUTPUT_VARIABLE sdk_sha OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT sdk_sha STREQUAL "b774170ff46c393eeb5e495ea37936038d3f4f4f")
    message(FATAL_ERROR "Near-metal PHY ABI requires pinned ESP-IDF b774170 (5.5.5)")
endif()
execute_process(COMMAND git -C "${PROJECT_DIR}" rev-parse HEAD OUTPUT_VARIABLE source_sha OUTPUT_STRIP_TRAILING_WHITESPACE)
file(GLOB_RECURSE source_files CONFIGURE_DEPENDS RELATIVE "${PROJECT_DIR}" "${PROJECT_DIR}/main/*" "${PROJECT_DIR}/vendor/*")
list(APPEND source_files "CMakeLists.txt" "sdkconfig.defaults" "partitions.csv")
list(SORT source_files)
set(source_pin "")
foreach(path IN LISTS source_files)
    if(NOT IS_DIRECTORY "${PROJECT_DIR}/${path}")
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${PROJECT_DIR}/${path}")
        file(SHA256 "${PROJECT_DIR}/${path}" one_hash)
        string(APPEND source_pin "${path}:${one_hash}\n")
    endif()
endforeach()
string(SHA256 source_digest "${source_pin}")
configure_file("${CMAKE_CURRENT_LIST_DIR}/source_identity.h.in" "${CMAKE_CURRENT_BINARY_DIR}/source_identity.h" @ONLY)
target_include_directories(${COMPONENT_LIB} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
