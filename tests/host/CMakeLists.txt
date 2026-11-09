cmake_minimum_required(VERSION 3.16)

project(wemos_r32_host_tests C)

enable_testing()

add_executable(sedentary_reminder_test
    sedentary_reminder_test.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../components/wellness_monitor/sedentary_reminder.c
)

target_include_directories(sedentary_reminder_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../components/wellness_monitor
)

target_compile_features(sedentary_reminder_test PRIVATE c_std_11)

if(MSVC)
    target_compile_options(sedentary_reminder_test PRIVATE /W4 /WX)
else()
    target_compile_options(sedentary_reminder_test PRIVATE
        -Wall -Wextra -Werror -pedantic
    )
endif()

add_test(NAME sedentary_reminder_test COMMAND sedentary_reminder_test)

add_executable(feedback_player_test
    feedback_player_test.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../components/feedback_service/feedback_player.c
)

target_include_directories(feedback_player_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../components/feedback_service
)

target_compile_features(feedback_player_test PRIVATE c_std_11)

if(MSVC)
    target_compile_options(feedback_player_test PRIVATE /W4 /WX)
else()
    target_compile_options(feedback_player_test PRIVATE
        -Wall -Wextra -Werror -pedantic
    )
endif()

add_test(NAME feedback_player_test COMMAND feedback_player_test)

add_executable(pir_service_test
    pir_service_test.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../components/wellness_monitor/pir_service.c
)

target_include_directories(pir_service_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../components/wellness_monitor
)

target_compile_features(pir_service_test PRIVATE c_std_11)

if(MSVC)
    target_compile_options(pir_service_test PRIVATE /W4 /WX)
else()
    target_compile_options(pir_service_test PRIVATE
        -Wall -Wextra -Werror -pedantic
    )
endif()

add_test(NAME pir_service_test COMMAND pir_service_test)

add_executable(care_coordinator_test
    care_coordinator_test.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../components/wellness_monitor/care_coordinator.c
)

target_include_directories(care_coordinator_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../components/wellness_monitor
)

target_compile_features(care_coordinator_test PRIVATE c_std_11)

if(MSVC)
    target_compile_options(care_coordinator_test PRIVATE /W4 /WX)
else()
    target_compile_options(care_coordinator_test PRIVATE
        -Wall -Wextra -Werror -pedantic
    )
endif()

add_test(NAME care_coordinator_test COMMAND care_coordinator_test)

add_executable(water_reminder_test
    water_reminder_test.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../components/wellness_monitor/water_reminder.c
)

target_include_directories(water_reminder_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../components/wellness_monitor
)

target_compile_features(water_reminder_test PRIVATE c_std_11)

if(MSVC)
    target_compile_options(water_reminder_test PRIVATE /W4 /WX)
else()
    target_compile_options(water_reminder_test PRIVATE
        -Wall -Wextra -Werror -pedantic
    )
endif()

add_test(NAME water_reminder_test COMMAND water_reminder_test)

add_executable(startup_service_test
    startup_service_test.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../components/startup_service/startup_service.c
)

target_include_directories(startup_service_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../components/startup_service/include
)

target_compile_features(startup_service_test PRIVATE c_std_11)

if(MSVC)
    target_compile_options(startup_service_test PRIVATE /W4 /WX)
else()
    target_compile_options(startup_service_test PRIVATE
        -Wall -Wextra -Werror -pedantic
    )
endif()

add_test(NAME startup_service_test COMMAND startup_service_test)
