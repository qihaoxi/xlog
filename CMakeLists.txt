cmake_minimum_required(VERSION 3.18)
project(xlog C)

set(CMAKE_C_STANDARD 11)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if (WIN32)
	set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)
endif ()

include(cmake/flags.cmake)
include(cmake/sanitizer.cmake)

#Library source files
file(GLOB SOURCE_FILES
		${CMAKE_SOURCE_DIR}/src/*.c
)

# Public include directory for users of the library
set(XLOG_PUBLIC_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/include)
set(XLOG_PRIVATE_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/src)

# Build xlog library - only use internal headers
add_library(xlog STATIC ${SOURCE_FILES})
target_include_directories(xlog PRIVATE ${CMAKE_SOURCE_DIR}/src)

add_library(xlog_shared SHARED ${SOURCE_FILES})
target_include_directories(xlog_shared PRIVATE  ${XLOG_PRIVATE_INCLUDE_DIR})
set_target_properties(xlog_shared PROPERTIES OUTPUT_NAME xlog)

# Build examples
option(BUILD_EXAMPLES "Build example programs" ON)
if (BUILD_EXAMPLES)
	include(cmake/example.cmake)
endif ()

option(BUILD_TESTS "Build test programs" ON)
if (BUILD_TESTS)
	include(cmake/test.cmake)
endif ()

# Single header generation
include(cmake/compress.cmake)


