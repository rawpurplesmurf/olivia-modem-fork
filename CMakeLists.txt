cmake_minimum_required(VERSION 3.16)
project(oliviamodem VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(oliviamodem STATIC
    src/oliviamodem.cpp
)

target_include_directories(oliviamodem
    PUBLIC  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

# Math library (needed by pj_mfsk.h templates)
target_link_libraries(oliviamodem PUBLIC m)

# Install rules
include(GNUInstallDirs)
install(TARGETS oliviamodem
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
