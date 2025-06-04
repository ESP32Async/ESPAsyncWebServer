cmake_minimum_required(VERSION 3.10)

file(GLOB SOURCES src/*.cpp)

set(pub_requires arduino-esp32 esp_wifi network_provisioning)

idf_component_register(
    SRCS ${SOURCES}
    INCLUDE_DIRS src
    REQUIRES ${pub_requires}
)
