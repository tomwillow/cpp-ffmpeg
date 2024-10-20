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

file(GLOB SRC_CODE
	src/*.h
	src/*.cpp
)

# 目标exe
add_executable(${PROJECT_NAME} 
	${SRC_CODE}
)

target_include_directories(${PROJECT_NAME} PRIVATE ${FFMPEG_INCLUDE_DIRS})
target_link_directories(${PROJECT_NAME} PRIVATE ${FFMPEG_LIBRARY_DIRS})
target_link_libraries(${PROJECT_NAME} PRIVATE 
	${FFMPEG_LIBRARIES}
)

target_link_libraries(${PROJECT_NAME}
	PRIVATE
	$<TARGET_NAME_IF_EXISTS:SDL2::SDL2main>
	$<IF:$<TARGET_EXISTS:SDL2::SDL2>,SDL2::SDL2,SDL2::SDL2-static>
)

target_link_libraries(${PROJECT_NAME} PRIVATE spdlog::spdlog)