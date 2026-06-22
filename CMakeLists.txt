cmake_minimum_required(VERSION 3.16)

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

if(DEFINED ESP_PLATFORM)

  include($ENV{IDF_PATH}/tools/cmake/project.cmake)
  add_compile_definitions(
    CC_DEBUG=1
    CASE_SENSITIVE=1
    PAD_SIZE=64
    HEAP_SIZE=65536
    GIT_VERSION="${GIT_VERSION}"
  )
  project(rForth)

  set(LITTLEFS_IMAGE_SIZE  "0x200000" CACHE STRING "Image size")
  set(LITTLEFS_BLOCK_SIZE  "4096"     CACHE STRING "Block size")
  set(LITTLEFS_PAGE_SIZE   "256"      CACHE STRING "Page size")

  find_program(MKLITTLEFS mklittlefs)
  if(NOT MKLITTLEFS)
    message(FATAL_ERROR "mklittlefs not found")
  endif()

  set(SOURCE_DIR   ${CMAKE_SOURCE_DIR}/forth)
  set(OUTPUT_IMAGE ${CMAKE_BINARY_DIR}/littlefs.bin)

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
      0x10000    ${CMAKE_BINARY_DIR}/rForth.bin
      0x200000   ${CMAKE_BINARY_DIR}/littlefs.bin
    DEPENDS
      make_image
      ${CMAKE_BINARY_DIR}/rForth.bin
      ${CMAKE_BINARY_DIR}/bootloader/bootloader.bin
      ${CMAKE_BINARY_DIR}/partition_table/partition-table.bin
      ${CMAKE_BINARY_DIR}/ota_data_initial.bin
    VERBATIM
  )
else()

  project(rForth LANGUAGES C CXX)
  set(CMAKE_CXX_STANDARD 20)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)

  add_executable(rForth
    main/main_linux.cpp
    main/core/rForth.cpp
    main/platform/linux.cpp
  )

  target_include_directories(rForth PRIVATE
    main
    main/core
    main/platform
  )

  target_compile_definitions(rForth PRIVATE
    CC_DEBUG=1
    CASE_SENSITIVE=1
    PAD_SIZE=64
    HEAP_SIZE=65536
    GIT_VERSION="${GIT_VERSION}"
    LINUX_PLATFORM=1
  )

  target_link_libraries(rForth PRIVATE pthread)

  add_custom_target(run
    COMMAND ./rForth
    DEPENDS rForth
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Running rForth on Linux"
  )

endif()
