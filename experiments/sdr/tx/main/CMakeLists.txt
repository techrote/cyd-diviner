idf_component_register(SRCS "tx_probe.c" REQUIRES esp_wifi esp_phy esp_driver_uart esp_timer app_update)
execute_process(COMMAND git rev-parse HEAD WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}" OUTPUT_VARIABLE source_sha OUTPUT_STRIP_TRAILING_WHITESPACE)
file(SHA256 "${CMAKE_CURRENT_LIST_DIR}/tx_probe.c" source_digest)
target_compile_definitions(${COMPONENT_LIB} PRIVATE SDR_SOURCE_SHA="${source_sha}" SDR_SOURCE_DIGEST="${source_digest}")
