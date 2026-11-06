# The two original ship-part roles must draw distinct equipment silhouettes and colours.
cmake_minimum_required(VERSION 3.16)
foreach(var GAME WORK_DIR PIKMIN_STANDIN_DATA)
    if(NOT DEFINED ${var})
        message(FATAL_ERROR "ship-parts capture: ${var} is not set")
    endif()
endforeach()
file(MAKE_DIRECTORY "${WORK_DIR}")
set(shot "${WORK_DIR}/ship_parts.png")
execute_process(
    COMMAND ${CMAKE_COMMAND} -E env "PIKMIN_SCENARIO_DATA_DIR=${PIKMIN_STANDIN_DATA}"
        "SDL_AUDIODRIVER=dummy"
        "${GAME}" --scenario ship_parts_showcase --scenario-tick-rate 30
        --scenario-screenshot "120:${shot}"
    OUTPUT_VARIABLE out ERROR_VARIABLE err RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "ship-parts scenario failed (${rc}):\n${out}\n${err}")
endif()
find_package(Python3 COMPONENTS Interpreter REQUIRED)
execute_process(
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/verify_scenario_png.py" "${shot}"
        --expect-width 640 --expect-height 480 --require-non-blank
        # Engine left: brass housing, amber core, teal connectors.
        --region-colour 70,160,295,325:C9A04A:45:700
        --region-colour 120,175,240,235:FFA630:40:100
        --region-colour 70,220,290,330:23808A:40:200
        # Radar right: open brass hoop above its teal instrument base.
        --region-colour 420,175,505,250:C9A04A:45:100
        --region-colour 420,235,505,290:23808A:40:100
    RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "ship-parts picture lost its equipment colours or composition")
endif()
