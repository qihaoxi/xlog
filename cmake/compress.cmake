# compress.cmake - Generate single header file

# Create output directory in the current build tree
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/single_include)

# Collect all source files for dependency tracking
file(GLOB XLOG_HEADERS
    ${CMAKE_SOURCE_DIR}/src/*.h
    ${CMAKE_SOURCE_DIR}/include/*.h
)
file(GLOB XLOG_SOURCES
    ${CMAKE_SOURCE_DIR}/src/*.c
)

# Output file path
set(SINGLE_HEADER_OUTPUT ${CMAKE_BINARY_DIR}/single_include/xlog.h)

# Add compress target using shell script with proper dependencies
add_custom_command(
    OUTPUT ${SINGLE_HEADER_OUTPUT}
    COMMAND ${CMAKE_COMMAND} -E env
            SINGLE_OUT_DIR=${CMAKE_BINARY_DIR}/single_include
            bash ${CMAKE_SOURCE_DIR}/compress.sh
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    DEPENDS ${XLOG_HEADERS} ${XLOG_SOURCES} ${CMAKE_SOURCE_DIR}/compress.sh
    COMMENT "Generating single header xlog.h..."
    VERBATIM
)

add_custom_target(compress DEPENDS ${SINGLE_HEADER_OUTPUT})

# Add alias
add_custom_target(single-header DEPENDS compress)

message(STATUS "compress target: Generate single header file (run 'make compress')")
