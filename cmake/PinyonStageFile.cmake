# Copies one build output beside the game executable when it differs.
#
#   cmake -DSOURCE=<file> -DDESTINATION=<file> -P PinyonStageFile.cmake
#
# `cmake -E copy_if_different` fails at once with only "Error copying file",
# without naming the reason. The destination is often held open for a moment
# by antivirus scanning a freshly written DLL, or for longer by a running game
# or tool, so retry briefly and then name the file, the reason and the fix.
cmake_minimum_required(VERSION 3.25)

foreach(_variable SOURCE DESTINATION)
    if(NOT DEFINED ${_variable} OR "${${_variable}}" STREQUAL "")
        message(FATAL_ERROR "PinyonStageFile.cmake requires -D${_variable}=<path>.")
    endif()
endforeach()
if(NOT EXISTS "${SOURCE}")
    message(FATAL_ERROR "Could not stage ${DESTINATION}: the built file ${SOURCE} is missing.")
endif()
if(NOT DEFINED ATTEMPTS)
    set(ATTEMPTS 10)
endif()

get_filename_component(_name "${DESTINATION}" NAME)
set(_result "")
foreach(_attempt RANGE 1 ${ATTEMPTS})
    file(COPY_FILE "${SOURCE}" "${DESTINATION}" RESULT _result ONLY_IF_DIFFERENT)
    if(_result STREQUAL "0")
        if(_attempt GREATER 1)
            message(STATUS "Staged ${_name} after ${_attempt} attempts.")
        endif()
        return()
    endif()
    if(_attempt LESS ATTEMPTS)
        if(_attempt EQUAL 1)
            message(STATUS "${_name} is in use (${_result}); retrying ${ATTEMPTS} times, one second apart.")
        endif()
        execute_process(COMMAND "${CMAKE_COMMAND}" -E sleep 1)
    endif()
endforeach()

message(FATAL_ERROR
    "Could not replace ${_name} beside the game after ${ATTEMPTS} attempts: ${_result}\n"
    "  Destination: ${DESTINATION}\n"
    "  The file is most likely open in another program. Close Pinyon Shift "
    "(pinyon_shift.exe) and any tool started from this folder, wait for "
    "antivirus scanning to finish or allow the Pinyon Shift folder in your "
    "antivirus, then run setup again.")
