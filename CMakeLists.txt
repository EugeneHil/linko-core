cmake_minimum_required(VERSION 3.28)

project(linko_core
  VERSION 0.1.0
  DESCRIPTION "Linko core: pure C++17 messenger core shared by all Linko builds"
  LANGUAGES CXX)

# Pure C++17, no platform dependencies, no compiler extensions.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Third-party dependencies (FetchContent, exact tags) are included before the
# warning flags below so their targets never inherit our -Werror flags.
include(cmake/Dependencies.cmake)

# Warnings are errors on every compiler.
if(MSVC)
  add_compile_options(/W4 /WX)
else()
  add_compile_options(-Wall -Wextra -Wpedantic -Werror)
endif()

# The core library: pure value types and pure functions only (design spec 4.2).
# src/linko.cpp anchors the static library; real units land in src/ with their
# public headers in include/linko/.
add_library(linko_core STATIC src/linko.cpp src/envelope.cpp src/messages.cpp)
target_include_directories(linko_core
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
# Envelope headers expose nlohmann/json, so propagate it to every consumer.
target_link_libraries(linko_core PUBLIC nlohmann_json::nlohmann_json)

enable_testing()
add_subdirectory(tests)
