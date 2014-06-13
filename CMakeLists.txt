cmake_minimum_required(VERSION 2.8.7)
project(zpython)

set(CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")

find_package(Zsh REQUIRED)
include_directories(SYSTEM ${ZSH_INCLUDE_DIR})

find_package(PythonLibs REQUIRED)
include_directories(SYSTEM ${PYTHON_INCLUDE_DIRS})
link_libraries(${PYTHON_LIBRARIES})

file(GLOB ZPYTHON_SOURCES src/*.c)

set(ZPYTHON_COMMAND_NAME "zpython"
    CACHE STRING "Zpython command and library name")
configure_file(
    "${PROJECT_SOURCE_DIR}/config/config.h.in"
    "${PROJECT_BINARY_DIR}/config/config.h"
)
include_directories("${PROJECT_BINARY_DIR}/config")
list(APPEND ZPYTHON_SOURCES "${PROJECT_BINARY_DIR}/config/config.h")

set(ZPYTHON_MAN_DIR "${CMAKE_INSTALL_PREFIX}/share/man"
    CACHE PATH "Path where zpython documentation will be installed")
file(GLOB ZPYTHON_DOC_SOURCES src/*.yo)
find_program(YODL_PRG yodl)
add_custom_command(
    OUTPUT "${PROJECT_BINARY_DIR}/zpython.1"
    COMMAND ${YODL_PRG} -o "${PROJECT_BINARY_DIR}/zpython.1"
                        -I"${PROJECT_SOURCE_DIR}/doc"
                        -w "${PROJECT_SOURCE_DIR}/doc/zman.yo"
                        "${PROJECT_SOURCE_DIR}/doc/zpython.yo"
    DEPENDS ${ZPYTHON_DOC_SOURCES}
)
add_custom_target(
    doc
    DEPENDS "${PROJECT_BINARY_DIR}/zpython.1"
)
install(FILES "${PROJECT_BINARY_DIR}/zpython.1"
        DESTINATION "${ZPYTHON_MAN_DIR}/man1"
        OPTIONAL)

add_library("${ZPYTHON_COMMAND_NAME}" MODULE ${ZPYTHON_SOURCES})
install(TARGETS "${ZPYTHON_COMMAND_NAME}"
        DESTINATION ${ZSH_MODULES_OUTPUT_DIR})
