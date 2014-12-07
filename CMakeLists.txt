cmake_minimum_required(VERSION 2.8.7)
project(zpython)

set(CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")

file(GLOB ZPYTHON_SOURCES src/*.c)

find_package(PythonLibs REQUIRED)
include_directories(SYSTEM ${PYTHON_INCLUDE_DIRS})
link_libraries(${PYTHON_LIBRARIES})

if(ZSH_REPOSITORY)
    set(ZHDIR "${PROJECT_BINARY_DIR}/include/zsh")
    file(MAKE_DIRECTORY "${PROJECT_BINARY_DIR}/include")
    file(MAKE_DIRECTORY "${ZHDIR}")
    file(GLOB ZHFILES "${ZSH_REPOSITORY}/Src/*.h")
    list(APPEND ZHFILES "${ZSH_REPOSITORY}/Src/zsh.mdh")
    file(GLOB ZEFILES "${ZSH_REPOSITORY}/Src/*.epro")
    list(APPEND ZEFILES "${ZSH_REPOSITORY}/config.h")
    foreach(zhfile ${ZHFILES})
        get_filename_component(f ${zhfile} NAME)
        add_custom_command(
            OUTPUT "${ZHDIR}/${f}"
            COMMAND "${PROJECT_SOURCE_DIR}/process_zsh_header.sh" "${zhfile}" "${ZHDIR}/${f}"
            DEPENDS "${PROJECT_SOURCE_DIR}/process_zsh_header.sh" "${zhfile}"
        )
        list(APPEND ZPYTHON_SOURCES "${ZHDIR}/${f}")
    endforeach()
    foreach(zefile ${ZEFILES})
        get_filename_component(f ${zefile} NAME)
        add_custom_command(
            OUTPUT "${ZHDIR}/${f}"
            COMMAND cp "${zefile}" "${ZHDIR}/${f}"
            DEPENDS "${zefile}"
        )
        list(APPEND ZPYTHON_SOURCES "${ZHDIR}/${f}")
    endforeach()
    list(APPEND ZHFILES ${ZEFILES})
    include_directories(SYSTEM "${PROJECT_BINARY_DIR}/include")
    set(ZSH_EXECUTABLE "${ZSH_REPOSITORY}/Src/zsh")
    EXECUTE_PROCESS(
        COMMAND ${ZSH_EXECUTABLE} -fc "echo -n $ZSH_VERSION"
        OUTPUT_VARIABLE ZSH_VERSION_STRING
    )

    EXECUTE_PROCESS(
        COMMAND ${ZSH_EXECUTABLE} -fc "echo -n $module_path[1]"
        OUTPUT_VARIABLE ZSH_MODULES_OUTPUT_DIR
    )
else()
    find_package(Zsh REQUIRED)
    include_directories(SYSTEM ${ZSH_INCLUDE_DIR})
endif()

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
install(
    FILES "${PROJECT_BINARY_DIR}/zpython.1"
    DESTINATION "${ZPYTHON_MAN_DIR}/man1"
    OPTIONAL
)

add_library("${ZPYTHON_COMMAND_NAME}" MODULE ${ZPYTHON_SOURCES})
install(TARGETS "${ZPYTHON_COMMAND_NAME}"
        DESTINATION ${ZSH_MODULES_OUTPUT_DIR})

enable_testing()
add_test(
    NAME test
    COMMAND ${ZSH_EXECUTABLE} +Z -f "${PROJECT_SOURCE_DIR}/test/runtest.zsh"
                              "${ZSH_EXECUTABLE}"
                              "${PROJECT_SOURCE_DIR}"
                              "${PROJECT_BINARY_DIR}"
                              "${ZPYTHON_COMMAND_NAME}"
)
