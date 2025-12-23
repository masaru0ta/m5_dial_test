# M5Dial Hello World Project
cmake_minimum_required(VERSION 3.16)

# ESP-IDFのコンポーネントパスを設定
set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/components"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(m5dial-hello)
