# === target =============
set(TARGET_NAME "cppffmpeg")

file(GLOB SRC_CODE
	*.h
	*.cpp
)

add_library(${TARGET_NAME} STATIC
	${SRC_CODE}
)

set_target_properties(${TARGET_NAME} PROPERTIES LINKER_LANGUAGE CXX)

target_include_directories(${TARGET_NAME} PUBLIC
	..
	${FFMPEG_INCLUDE_DIRS}
)
target_link_directories(${TARGET_NAME} PUBLIC 
	${FFMPEG_LIBRARY_DIRS})

target_link_libraries(${TARGET_NAME} PUBLIC 
	spdlog::spdlog
	$<TARGET_NAME_IF_EXISTS:SDL2::SDL2main>
	$<IF:$<TARGET_EXISTS:SDL2::SDL2>,SDL2::SDL2,SDL2::SDL2-static>
	${FFMPEG_LIBRARIES}
)