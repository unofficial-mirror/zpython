cmake_minimum_required(VERSION 2.8.7)
project(zpython)

set(CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")

find_package(Zsh REQUIRED)
include_directories(SYSTEM ${ZSH_INCLUDE_DIR})

find_package(PythonLibs REQUIRED)
include_directories(SYSTEM ${PYTHON_INCLUDE_DIRS})
link_libraries(${PYTHON_LIBRARIES})

file(GLOB ZPYTHON_SOURCES src/*.c)

set(ZPYTHON_COMMAND_NAME "zpython" CACHE STRING "Zpython command and library name")
configure_file(
    "${PROJECT_SOURCE_DIR}/config/config.h.in"
    "${PROJECT_BINARY_DIR}/config/config.h"
)
include_directories("${PROJECT_BINARY_DIR}/config")
list(APPEND ZPYTHON_SOURCES "${PROJECT_BINARY_DIR}/config/config.h")

add_library("${ZPYTHON_COMMAND_NAME}" MODULE ${ZPYTHON_SOURCES})
install(TARGETS "${ZPYTHON_COMMAND_NAME}"
        DESTINATION ${ZSH_MODULES_OUTPUT_DIR})
