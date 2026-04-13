set(TEST_OUTPUT_DIR ${CMAKE_BINARY_DIR}/tests)
file(MAKE_DIRECTORY ${TEST_OUTPUT_DIR})

function(add_xlog_test target_name source_file)
    add_executable(${target_name} ${source_file})
    target_link_libraries(${target_name} xlog)
    # Tests can use both public API (include/) and internal headers (src/)
    target_include_directories(${target_name} PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src
    )
    set_target_properties(${target_name} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${TEST_OUTPUT_DIR})
endfunction()

function(add_xlog_test_with_sources target_name)
    add_executable(${target_name} ${ARGN})
    target_link_libraries(${target_name} xlog)
    target_include_directories(${target_name} PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src
    )
    set_target_properties(${target_name} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${TEST_OUTPUT_DIR})
endfunction()

add_xlog_test(test_ringbuf ${CMAKE_SOURCE_DIR}/tests/test_ringbuf.c)
add_xlog_test(test_log_record ${CMAKE_SOURCE_DIR}/tests/test_log_record.c)
add_xlog_test(test_xlog ${CMAKE_SOURCE_DIR}/tests/test_xlog.c)
add_xlog_test(test_rotate ${CMAKE_SOURCE_DIR}/tests/test_rotate.c)
add_xlog_test(test_file_sink ${CMAKE_SOURCE_DIR}/tests/test_file_sink.c)
add_xlog_test(test_simd ${CMAKE_SOURCE_DIR}/tests/test_simd.c)
add_xlog_test(test_color ${CMAKE_SOURCE_DIR}/tests/test_color.c)
add_xlog_test(test_syslog_sink ${CMAKE_SOURCE_DIR}/tests/test_syslog_sink.c)
add_xlog_test(test_builder ${CMAKE_SOURCE_DIR}/tests/test_builder.c)
add_xlog_test(test_legacy_macros ${CMAKE_SOURCE_DIR}/tests/test_legacy_macros.c)
add_xlog_test(test_formatter ${CMAKE_SOURCE_DIR}/tests/test_formatter.c)
add_xlog_test(test_compress ${CMAKE_SOURCE_DIR}/tests/test_compress.c)
add_xlog_test(test_ringbuf_concurrency ${CMAKE_SOURCE_DIR}/tests/test_ringbuf_concurrency.c)
add_xlog_test(bench_xlog_perf ${CMAKE_SOURCE_DIR}/tests/bench_xlog_perf.c)

# test_single_header is special: uses single header, no library linking
# Depends on compress target to generate single_include/xlog.h first
add_executable(test_single_header ${CMAKE_SOURCE_DIR}/tests/test_single_header.c)
target_include_directories(test_single_header PRIVATE ${CMAKE_BINARY_DIR}/single_include)
target_link_libraries(test_single_header pthread)
set_target_properties(test_single_header PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${TEST_OUTPUT_DIR})
add_dependencies(test_single_header compress)

