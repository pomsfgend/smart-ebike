# Standalone bench project for the fast-charge add-on (ESP-IDF 5.0.9, ESP32-C3, same as the spoof).
# For the real product the component goes into the spoof tree instead, see docs/INTEGRATION_WITH_SPOOF.md.
cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(FC_GBT_BENCH)
