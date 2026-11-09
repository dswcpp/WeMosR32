set(wifi_ini_embed_files)
set(wifi_ini_path "${CMAKE_CURRENT_LIST_DIR}/../../wifi.ini")
if(EXISTS "${wifi_ini_path}")
    list(APPEND wifi_ini_embed_files "../../wifi.ini")
endif()

idf_component_register(
    SRCS "app_config.c"
    INCLUDE_DIRS "include"
    REQUIRES wifi_manager
    EMBED_FILES ${wifi_ini_embed_files}
)

if(EXISTS "${wifi_ini_path}")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE APP_WIFI_INI_EMBEDDED)
endif()

target_compile_definitions(${COMPONENT_LIB} PRIVATE APP_VERSION="${PROJECT_VER}")

