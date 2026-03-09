option(DEBUG "Enable debug" ON)
option(STRICT_CHECKS "Enable strict warnings" OFF)
option(ENABLE_SIMD "Enable SIMD optimizations" ON)


# check os is centos7, set C compiler to /usr/local/gcc/bin/gcc
function(is_centos7 result)
	execute_process(COMMAND sh -c "cat /etc/centos-release | grep 'CentOS Linux release 7'" OUTPUT_VARIABLE CENTOS7_CHECK OUTPUT_STRIP_TRAILING_WHITESPACE)
	if (CENTOS7_CHECK)
		set(${result} TRUE PARENT_SCOPE)
	else ()
		set(${result} FALSE PARENT_SCOPE)
	endif ()
endfunction()

is_centos7(CENTOS7)
if (CENTOS7)
	set(CMAKE_C_COMPILER /usr/local/gcc/bin/gcc)
endif()

if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fmacro-prefix-map=${CMAKE_SOURCE_DIR}/=")
	#    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fdebug-prefix-map=${CMAKE_SOURCE_DIR}/=")

	if (DEBUG)
		set(CMAKE_BUILD_TYPE Debug)
		set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g -ggdb3 -O0")
	endif ()

	if (STRICT_CHECKS)
		set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -D_GNU_SOURCE -D_FILE_OFFSET_BITS=64 \
            -D_LARGEFILE64_SOURCE -D_LARGEFILE_SOURCE -Wall -Wextra -Werror -Wmissing-prototypes \
                        -Wno-sign-compare -Wstrict-prototypes -Wmissing-declarations \
                        -Wwrite-strings -fstack-protector -fstack-protector-strong -fstack-clash-protection \
                        -Wcast-align -Wuninitialized -Wno-unused-function\
                        -fPIC -rdynamic"
		)
	else()
		set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -D_GNU_SOURCE -D_FILE_OFFSET_BITS=64 \
            -D_LARGEFILE64_SOURCE -D_LARGEFILE_SOURCE -Werror \
                        -Wno-sign-compare -Wstrict-prototypes \
                        -Wwrite-strings -fstack-protector -fstack-protector-strong -fstack-clash-protection \
                        -Wcast-align -Wuninitialized -fPIC -rdynamic"
		)
	endif ()
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -pthread")

	# SIMD flags for Linux
	if (ENABLE_SIMD)
		include(CheckCCompilerFlag)
		check_c_compiler_flag("-msse4.2" HAS_SSE42)
		check_c_compiler_flag("-mavx2" HAS_AVX2)

		if (HAS_SSE42)
			set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -msse4.2")
		endif()
		if (HAS_AVX2)
			set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -mavx2")
		endif()
	endif()
endif ()

if (CMAKE_SYSTEM_NAME STREQUAL "Darwin")
	message(STATUS "Configuring for macOS")

	if (DEBUG)
		set(CMAKE_BUILD_TYPE Debug)
		set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g -O0")
	endif()

	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -D_GNU_SOURCE -Wall -pthread")

	# SIMD flags for macOS (both Intel and Apple Silicon)
	if (ENABLE_SIMD)
		# Check architecture
		execute_process(COMMAND uname -m OUTPUT_VARIABLE MACOS_ARCH OUTPUT_STRIP_TRAILING_WHITESPACE)
		if (MACOS_ARCH STREQUAL "arm64")
			message(STATUS "macOS ARM64 detected - NEON is enabled by default")
		else()
			include(CheckCCompilerFlag)
			check_c_compiler_flag("-msse4.2" HAS_SSE42)
			check_c_compiler_flag("-mavx2" HAS_AVX2)

			if (HAS_SSE42)
				set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -msse4.2")
			endif()
			if (HAS_AVX2)
				set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -mavx2")
			endif()
		endif()
	endif()
endif ()

if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
	message(STATUS "Configuring for Windows")

	if (MSVC)
		if (DEBUG)
			set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /Zi /Od")
		endif()

		set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /W3")

		# SIMD flags for MSVC
		if (ENABLE_SIMD)
			# MSVC enables SSE2 by default on x64
			# AVX2 requires explicit flag
			check_c_compiler_flag("/arch:AVX2" HAS_AVX2)
			if (HAS_AVX2)
				set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /arch:AVX2")
			endif()
		endif()
	else()
		# MinGW/Clang on Windows
		if (DEBUG)
			set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g -O0")
		endif()

		if (ENABLE_SIMD)
			include(CheckCCompilerFlag)
			check_c_compiler_flag("-msse4.2" HAS_SSE42)
			check_c_compiler_flag("-mavx2" HAS_AVX2)

			if (HAS_SSE42)
				set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -msse4.2")
			endif()
			if (HAS_AVX2)
				set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -mavx2")
			endif()
		endif()
	endif()
endif ()
