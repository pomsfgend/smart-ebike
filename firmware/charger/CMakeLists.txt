# Charger controller firmware (ESP-IDF 5.0.x like the cube, target esp32c3).
cmake_minimum_required(VERSION 3.16)
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../common")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(FC_CHARGER)
