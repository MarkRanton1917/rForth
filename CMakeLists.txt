cmake_minimum_required(VERSION 3.16)
set (CMAKE_CXX_STANDARD 20)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
execute_process(
  COMMAND git describe --tags --abbrev=7
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  OUTPUT_VARIABLE GIT_VERSION
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET
)
if(NOT GIT_VERSION)
  execute_process(
    COMMAND git rev-parse --short HEAD
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_VERSION
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
  )
endif()
add_definitions(
  -DCC_DEBUG=1 
  -DCASE_SENSITIVE=1 
  -DPAD_SIZE=64
  -DHEAP_SIZE=65536
)
add_definitions(
  -DGIT_VERSION="${GIT_VERSION}" 
  -DARDUINO_USB_MODE=1 
  -DARDUINO_USB_CDC_ON_BOOT=1
)
project(skink)
set(ESP_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/components/arduino" CACHE PATH "Path to Arduino component")
