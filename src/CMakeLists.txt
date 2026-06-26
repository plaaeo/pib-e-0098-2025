# This file was automatically generated for projects
# without default 'CMakeLists.txt' file.

idf_component_register(
    SRCS
    ${CMAKE_SOURCE_DIR}/src/lora/experimental.cc
    ${CMAKE_SOURCE_DIR}/src/lora/radios/radiolib.cc
    ${CMAKE_SOURCE_DIR}/src/net/trickle.cc
    ${CMAKE_SOURCE_DIR}/src/net/types.cc
    ${CMAKE_SOURCE_DIR}/src/net/packets.cc
    ${CMAKE_SOURCE_DIR}/src/port/esp32/main.cc
    ${CMAKE_SOURCE_DIR}/src/port/esp32/port.cc
    ${CMAKE_SOURCE_DIR}/src/sensor/interface.cc
)
