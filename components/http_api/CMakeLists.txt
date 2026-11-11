idf_component_register(
    SRCS "config_api.c"
    INCLUDE_DIRS "include"
    REQUIRES
        app_config
        app_events
        button_service
        config_service
        esp_http_server
        json
        stock_service
        wifi_manager
    EMBED_FILES
        "www/manifest.webmanifest"
        "www/icon.svg"
)

find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(WEB_UI_GZIP_BUDGET_BYTES 65536)
set(index_html "${CMAKE_CURRENT_LIST_DIR}/www/index.html")
set(index_html_gzip "${CMAKE_CURRENT_BINARY_DIR}/www/index.html.gz")

add_custom_command(
    OUTPUT "${index_html_gzip}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory
            "${CMAKE_CURRENT_BINARY_DIR}/www"
    COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_LIST_DIR}/tools/gzip_file.py"
            "${index_html}" "${index_html_gzip}"
            "${WEB_UI_GZIP_BUDGET_BYTES}"
    DEPENDS "${index_html}"
            "${CMAKE_CURRENT_LIST_DIR}/tools/gzip_file.py"
    COMMENT "Compressing web UI"
    VERBATIM
)

target_add_binary_data(
    ${COMPONENT_LIB}
    "${index_html_gzip}"
    BINARY
    RENAME_TO index_html_gz
)
