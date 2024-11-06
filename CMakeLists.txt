cmake_minimum_required(VERSION 3.11)

# C++17
set(CMAKE_CXX_STANDARD 17)

add_compile_options("$<$<CXX_COMPILER_ID:MSVC>:/source-charset:utf-8>")

project(ffmpeg_demo)

# 添加unicode宏
add_definitions(-DUNICODE -D_UNICODE)
# === dependencies ==============

find_package(FFMPEG REQUIRED)

find_package(SDL2 CONFIG REQUIRED)

find_package(spdlog CONFIG REQUIRED)
# === entrance =============

add_subdirectory(src/cppffmpeg)
add_subdirectory(example)