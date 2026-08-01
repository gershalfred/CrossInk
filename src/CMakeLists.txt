# Application component definition for the Arduino + ESP-IDF hybrid build.
file(GLOB_RECURSE app_sources ${CMAKE_SOURCE_DIR}/src/*.*)

idf_component_register(SRCS ${app_sources} REQUIRES framework-arduinoespressif32)
