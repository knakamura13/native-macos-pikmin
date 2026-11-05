# standin_showcase capture at tick 120 with composition guards (issue #115).
cmake_minimum_required(VERSION 3.16)

foreach(var GAME WORK_DIR)
    if(NOT DEFINED ${var})
        message(FATAL_ERROR "check_scenario_showcase_screenshot.cmake: ${var} is not set")
    endif()
endforeach()

if(NOT DEFINED PIKMIN_STANDIN_DATA)
    message(FATAL_ERROR "check_scenario_showcase_screenshot.cmake: PIKMIN_STANDIN_DATA is not set")
endif()

set(capture_w 640)
set(capture_h 480)
set(pos_png "${WORK_DIR}/standin_showcase_t120.png")
file(MAKE_DIRECTORY "${WORK_DIR}")

execute_process(
    COMMAND ${CMAKE_COMMAND} -E env "PIKMIN_SCENARIO_DATA_DIR=${PIKMIN_STANDIN_DATA}"
        "${GAME}" --scenario standin_showcase --scenario-tick-rate 30
        --scenario-screenshot "120:${pos_png}"
    OUTPUT_VARIABLE out
    ERROR_VARIABLE err
    RESULT_VARIABLE rc)
message("${out}${err}")

if(NOT rc STREQUAL "0")
    message(FATAL_ERROR "standin_showcase with screenshot exited ${rc}")
endif()

find_package(Python3 COMPONENTS Interpreter REQUIRED)
set(verify "${CMAKE_CURRENT_LIST_DIR}/verify_scenario_png.py")

execute_process(
    COMMAND "${Python3_EXECUTABLE}" "${verify}" "${pos_png}"
        --expect-width ${capture_w}
        --expect-height ${capture_h}
        --require-non-blank
        --min-distinct-colors 120
        --min-variance 120.0
        --max-near-black-ratio 0.20
        --min-center-non-grass 800
        --min-center-distinct 10
        --min-saturated-red 350
        --min-saturated-yellow 350
        --min-saturated-blue 350
        --max-center-near-black-ratio 0.18
        # Back left, the red hive: warm grey stone with the red crystals of its
        # type at the top (the crystals are effects/shapes/r_goal.mod, which the
        # game draws only while the hive is booted). Back right, the ship: a teal
        # hull under an amber lamp. Nothing run off the frame.
        --onion-region 15,0,205,120 --min-onion-red 2200
        --region-colour 15,0,205,280:807868:24:9000
        --region-colour 355,115,610,265:208088:36:3000
        --region-colour 355,40,610,140:F0A028:30:1200
        --max-edge-object-pixels 20
    RESULT_VARIABLE pos_rc)
if(NOT pos_rc STREQUAL "0")
    message(FATAL_ERROR "standin_showcase PNG composition check failed")
endif()

message("standin_showcase screenshot checks passed")
