cmake_minimum_required(VERSION 3.16)

# Add LovyanGFX from m5dial-hello
set(EXTRA_COMPONENT_DIRS "../m5dial-hello/components")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(m5dial-led)
