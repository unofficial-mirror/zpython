cmake_minimum_required(VERSION 2.8.7)
project(zpython)

set(CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")

find_package(Zsh REQUIRED)
find_package(PythonLibs REQUIRED)

include_directories(SYSTEM ${ZSH_INCLUDE_DIR})
include_directories(SYSTEM ${PYTHON_INCLUDE_DIRS})
link_libraries(${PYTHON_LIBRARIES})

file(GLOB ZPYTHON_SOURCES src/*.c)

add_library(zpython MODULE ${ZPYTHON_SOURCES})
install(TARGETS zpython
        DESTINATION ${ZSH_MODULES_OUTPUT_DIR})
