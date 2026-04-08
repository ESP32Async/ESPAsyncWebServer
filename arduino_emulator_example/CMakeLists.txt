cmake_minimum_required(VERSION 3.11)
project(espasyncwebserver_host_compile LANGUAGES C CXX)

add_subdirectory(${CMAKE_SOURCE_DIR}/../.ci/arduino-emulator ${CMAKE_BINARY_DIR}/arduino-emulator)
file(GLOB WEB_SRC "${CMAKE_SOURCE_DIR}/../src/*.cpp")
add_library(espasyncwebserver STATIC ${WEB_SRC})

target_compile_definitions(espasyncwebserver PUBLIC HOST ARDUINO=10813)
target_include_directories(espasyncwebserver PUBLIC
  ${CMAKE_SOURCE_DIR}/../src
  ${CMAKE_SOURCE_DIR}/../.ci/asynctcp/src
  ${CMAKE_SOURCE_DIR}/../.ci/arduino-esp32/libraries/FS/src
  ${CMAKE_SOURCE_DIR}/../.ci/lwip/src/include
  ${CMAKE_SOURCE_DIR}/../.ci/lwip-contrib/ports/unix/port/include
  ${CMAKE_SOURCE_DIR}
)
target_link_libraries(espasyncwebserver PUBLIC arduino_emulator)
