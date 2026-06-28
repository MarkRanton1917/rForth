set(SRC_FILES "core/rForth.cpp")
set(INC_DIRS "." "core")

if(HARDWARE STREQUAL "ESP32-USART")
  list(APPEND SRC_FILES "platform/esp32-usart.cpp")
endif()

idf_component_register(
  SRCS
    ${SRC_FILES}
  INCLUDE_DIRS
    ${INC_DIRS}
  REQUIRES
    nvs_flash
    esp_timer
    esp_driver_usb_serial_jtag
    joltwallet__littlefs
)
