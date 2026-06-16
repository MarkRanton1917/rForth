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

project(forth)
set(ESP_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/components/arduino" CACHE PATH "Path to Arduino component")

set(LITTLEFS_IMAGE_SIZE  "0x200000" CACHE STRING "Image size")
set(LITTLEFS_BLOCK_SIZE  "4096"     CACHE STRING "Block size")
set(LITTLEFS_PAGE_SIZE   "256"      CACHE STRING "Page size")

find_program(MKLITTLEFS mklittlefs)
if(NOT MKLITTLEFS)
    message(FATAL_ERROR "mklittefs not found")
endif()

set(SOURCE_DIR   ${CMAKE_SOURCE_DIR}/forth)
set(OUTPUT_IMAGE ${CMAKE_BINARY_DIR}/littlefs.bin)

if(NOT IS_DIRECTORY ${SOURCE_DIR})
  message(WARNING "Folder '${SOURCE_DIR}' not exists")
endif()

add_custom_command(
  OUTPUT ${OUTPUT_IMAGE}
  COMMAND ${MKLITTLEFS}
    -c ${SOURCE_DIR}
    -b ${LITTLEFS_BLOCK_SIZE}
    -p ${LITTLEFS_PAGE_SIZE}
    -s ${LITTLEFS_IMAGE_SIZE}
    ${OUTPUT_IMAGE}
  DEPENDS ${SOURCE_DIR}
  VERBATIM
)

add_custom_target(make_image ALL DEPENDS ${OUTPUT_IMAGE})

set(ESPTOOL_PORT "/dev/ttyACM0" CACHE STRING "Serial port")
set(ESPTOOL_BAUD "460800" CACHE STRING "Serial baudrate")
set(ESPTOOL_CHIP "esp32c3" CACHE STRING "Chip model")

add_custom_target(flash_all
  COMMAND python3 -m esptool
    --chip ${ESPTOOL_CHIP}
    --port ${ESPTOOL_PORT}
    -b ${ESPTOOL_BAUD}
    --before default_reset
    --after hard_reset
    write_flash
    --flash_mode dio
    --flash_size 4MB
    --flash_freq 80m
    0x0        ${CMAKE_BINARY_DIR}/bootloader/bootloader.bin
    0x8000     ${CMAKE_BINARY_DIR}/partition_table/partition-table.bin
    0xe000     ${CMAKE_BINARY_DIR}/ota_data_initial.bin
    0x10000    ${CMAKE_BINARY_DIR}/forth.bin
    0x200000   ${CMAKE_BINARY_DIR}/littlefs.bin
  DEPENDS
    make_image
    ${CMAKE_BINARY_DIR}/forth.bin
    ${CMAKE_BINARY_DIR}/bootloader/bootloader.bin
    ${CMAKE_BINARY_DIR}/partition_table/partition-table.bin
    ${CMAKE_BINARY_DIR}/ota_data_initial.bin
  VERBATIM
)
