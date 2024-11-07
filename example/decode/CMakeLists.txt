# === target =============
set(TARGET_NAME "example-decode")

file(GLOB SRC_CODE
	*.h
	*.cpp
)

# 目标exe
add_executable(${TARGET_NAME}
	${SRC_CODE}
)

target_link_libraries(${TARGET_NAME} PUBLIC 
	cppffmpeg
)