# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS ${CMAKE_CURRENT_LIST_DIR}/components/esp-homekit-sdk/components/homekit)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(token_ticker)
