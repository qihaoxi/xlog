set(example.cmake
		example_usage
		example_full
		example_simple
)

foreach (example ${example.cmake})
	add_executable(${example} ${CMAKE_SOURCE_DIR}/examples/${example}.c)
	target_link_libraries(${example} xlog)
	target_include_directories(${example} PRIVATE ${CMAKE_SOURCE_DIR}/include)
	set_target_properties(${example} PROPERTIES RUNTIME_OUTPUT_DIRECTORY
			${CMAKE_BINARY_DIR}/examples)
endforeach ()

message(STATUS "examples: ${example.cmake}")
