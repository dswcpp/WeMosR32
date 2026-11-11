idf_component_register(
    SRCS
        "stock_intraday_seed.c"
        "stock_intraday_parser.c"
        "stock_quote_parser.c"
        "stock_service.c"
    INCLUDE_DIRS "include"
    REQUIRES
        config_service
        stock_analyzer_embedded
        wifi_manager
    PRIV_REQUIRES
        esp_http_client
        esp_timer
        freertos
        json
        mbedtls
)
