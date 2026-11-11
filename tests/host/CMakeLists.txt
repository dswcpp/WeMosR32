cmake_minimum_required(VERSION 3.16)

project(wemos_r32_host_tests C)

enable_testing()

set(STOCK_COMPONENT_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/../../components/stock_service")
set(MONITOR_COMPONENT_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/../../components/stock_monitor")
set(ANALYZER_COMPONENT_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/../../components/stock_analyzer_embedded")
set(RGB_COMPONENT_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/../../components/rgb_indicator")
set(FEEDBACK_COMPONENT_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/../../components/feedback_service")
set(STATUS_LED_COMPONENT_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/../../components/status_led")
set(STARTUP_COMPONENT_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/../../components/startup_service")
set(OLED_COMPONENT_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/../../components/oled_display")

function(configure_stock_test target)
    target_compile_features(${target} PRIVATE c_std_11)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /WX)
    else()
        target_compile_options(${target} PRIVATE
            -Wall -Wextra -Wpedantic -Werror)
    endif()
    add_test(NAME ${target} COMMAND ${target})
endfunction()

add_executable(stock_quote_parser_test
    stock_quote_parser_test.c
    ${STOCK_COMPONENT_DIR}/stock_quote_parser.c
)
target_include_directories(stock_quote_parser_test PRIVATE
    ${STOCK_COMPONENT_DIR}/include
)

add_executable(stock_intraday_parser_test
    stock_intraday_parser_test.c
    ${STOCK_COMPONENT_DIR}/stock_intraday_parser.c
)
target_include_directories(stock_intraday_parser_test PRIVATE
    ${STOCK_COMPONENT_DIR}/include
)

add_executable(stock_intraday_seed_test
    stock_intraday_seed_test.c
    ${STOCK_COMPONENT_DIR}/stock_intraday_parser.c
    ${STOCK_COMPONENT_DIR}/stock_intraday_seed.c
    ${ANALYZER_COMPONENT_DIR}/stock_analyzer.c
)
target_include_directories(stock_intraday_seed_test PRIVATE
    ${STOCK_COMPONENT_DIR}/include
    ${ANALYZER_COMPONENT_DIR}
)

add_executable(stock_monitor_state_test
    stock_monitor_state_test.c
    ${MONITOR_COMPONENT_DIR}/stock_monitor_state.c
)
target_include_directories(stock_monitor_state_test PRIVATE
    ${MONITOR_COMPONENT_DIR}/include
)

add_executable(stock_monitor_alert_test
    stock_monitor_alert_test.c
    ${MONITOR_COMPONENT_DIR}/stock_monitor_alert.c
)
target_include_directories(stock_monitor_alert_test PRIVATE
    ${MONITOR_COMPONENT_DIR}/include
    ${STOCK_COMPONENT_DIR}/include
)

add_executable(stock_decision_test
    stock_decision_test.c
    ${MONITOR_COMPONENT_DIR}/stock_decision.c
)
target_include_directories(stock_decision_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/stubs
    ${MONITOR_COMPONENT_DIR}/include
    ${STOCK_COMPONENT_DIR}/include
    ${ANALYZER_COMPONENT_DIR}
)

add_executable(stock_monitor_visual_test
    stock_monitor_visual_test.c
    ${MONITOR_COMPONENT_DIR}/stock_monitor_visual.c
)
target_include_directories(stock_monitor_visual_test PRIVATE
    ${MONITOR_COMPONENT_DIR}/include
    ${STOCK_COMPONENT_DIR}/include
)

add_executable(stock_monitor_ui_test
    stock_monitor_ui_test.c
    ${MONITOR_COMPONENT_DIR}/stock_decision.c
    ${MONITOR_COMPONENT_DIR}/stock_monitor_state.c
    ${MONITOR_COMPONENT_DIR}/stock_monitor_ui.c
    ${MONITOR_COMPONENT_DIR}/stock_monitor_visual.c
    ${OLED_COMPONENT_DIR}/oled_canvas.c
    ${OLED_COMPONENT_DIR}/third_party/u8g2/csrc/u8g2_font.c
    ${OLED_COMPONENT_DIR}/third_party/u8g2/fonts/u8g2_font_5x7_tf.c
    ${OLED_COMPONENT_DIR}/third_party/u8g2/fonts/u8g2_font_6x13B_tf.c
    ${OLED_COMPONENT_DIR}/third_party/u8g2/fonts/u8g2_font_8x13B_tf.c
    ${OLED_COMPONENT_DIR}/third_party/u8g2/fonts/u8g2_font_9x18B_tf.c
)
target_include_directories(stock_monitor_ui_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/stubs
    ${MONITOR_COMPONENT_DIR}/include
    ${STOCK_COMPONENT_DIR}/include
    ${ANALYZER_COMPONENT_DIR}
    ${OLED_COMPONENT_DIR}/include
    ${OLED_COMPONENT_DIR}/third_party/u8g2/csrc
)

add_executable(stock_analyzer_core_test
    ${ANALYZER_COMPONENT_DIR}/test_stock_analyzer.c
    ${ANALYZER_COMPONENT_DIR}/stock_analyzer.c
)
target_include_directories(stock_analyzer_core_test PRIVATE
    ${ANALYZER_COMPONENT_DIR}
)

add_executable(stock_market_core_test
    ${ANALYZER_COMPONENT_DIR}/test_stock_market.c
    ${ANALYZER_COMPONENT_DIR}/stock_market.c
    ${ANALYZER_COMPONENT_DIR}/stock_analyzer.c
)
target_include_directories(stock_market_core_test PRIVATE
    ${ANALYZER_COMPONENT_DIR}
)

add_executable(rgb_indicator_engine_test
    rgb_indicator_engine_test.c
    ${RGB_COMPONENT_DIR}/rgb_indicator_engine.c
)
target_include_directories(rgb_indicator_engine_test PRIVATE
    ${RGB_COMPONENT_DIR}/include
)

add_executable(vibration_patterns_test
    vibration_patterns_test.c
    ${FEEDBACK_COMPONENT_DIR}/feedback_player.c
    ${FEEDBACK_COMPONENT_DIR}/vibration_patterns.c
)
target_include_directories(vibration_patterns_test PRIVATE
    ${FEEDBACK_COMPONENT_DIR}/include
    ${FEEDBACK_COMPONENT_DIR}
)

add_executable(status_led_pattern_test
    status_led_pattern_test.c
    ${STATUS_LED_COMPONENT_DIR}/status_led_pattern.c
)
target_include_directories(status_led_pattern_test PRIVATE
    ${STATUS_LED_COMPONENT_DIR}/include
)

add_executable(startup_service_test
    startup_service_test.c
    ${STARTUP_COMPONENT_DIR}/startup_service.c
)
target_include_directories(startup_service_test PRIVATE
    ${STARTUP_COMPONENT_DIR}/include
)

foreach(stock_test IN ITEMS
        stock_intraday_parser_test
        stock_intraday_seed_test
        stock_quote_parser_test
        stock_monitor_state_test
        stock_monitor_alert_test
        stock_decision_test
        stock_monitor_visual_test
        stock_monitor_ui_test
        stock_analyzer_core_test
        stock_market_core_test
        rgb_indicator_engine_test
        vibration_patterns_test
        status_led_pattern_test
        startup_service_test)
    configure_stock_test(${stock_test})
endforeach()

if(NOT MSVC)
    target_link_libraries(stock_intraday_parser_test PRIVATE m)
    target_link_libraries(stock_intraday_seed_test PRIVATE m)
    target_link_libraries(stock_monitor_visual_test PRIVATE m)
    target_link_libraries(stock_monitor_alert_test PRIVATE m)
    target_link_libraries(stock_decision_test PRIVATE m)
    target_link_libraries(stock_monitor_ui_test PRIVATE m)
    target_link_libraries(stock_analyzer_core_test PRIVATE m)
    target_link_libraries(stock_market_core_test PRIVATE m)
endif()
