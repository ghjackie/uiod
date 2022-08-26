cmake_minimum_required(VERSION 3.1)

project (uiod)

if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "MSVC")
	add_definitions(-DWIN32_LEAN_AND_MEAN -D_CRT_SECURE_NO_WARNINGS)
endif()

file(GLOB HEADER_FILES
    "*.h"
	)

file(GLOB SOURCE_FILES
    "*.cpp"
	)

add_executable (${PROJECT_NAME} ${SOURCE_FILES} ${HEADER_FILES})

add_subdirectory(test)