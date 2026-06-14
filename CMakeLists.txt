cmake_minimum_required(VERSION 3.16)
set (CMAKE_CXX_STANDARD 20)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
execute_process(
    COMMAND git rev-parse --short HEAD
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_VERSION
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
add_definitions(-DGIT_VERSION="${GIT_VERSION}" -DARDUINO_USB_MODE=1 -DARDUINO_USB_CDC_ON_BOOT=1)
project(skink)
set(ESP_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/components/arduino" CACHE PATH "Path to Arduino component")
