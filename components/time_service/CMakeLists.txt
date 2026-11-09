idf_component_register(
    SRCS "time_service.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_http_client esp_netif esp_timer mbedtls
    EMBED_TXTFILES "certs/time_date_ca.pem"
)
