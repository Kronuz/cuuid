cmake_minimum_required(VERSION 3.16)
project(cuuid LANGUAGES CXX)

include(FetchContent)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE Release)
endif()

# --- Dependencies (the Kronuz family, pinned by commit) ---
FetchContent_Declare(endian        GIT_REPOSITORY https://github.com/Kronuz/endian.git        GIT_TAG 8db745f5ccbf9b5c176bec27f900d64cf105c4c0)
FetchContent_Declare(char_classify GIT_REPOSITORY https://github.com/Kronuz/char-classify.git GIT_TAG 65c1ff5c4052d9108ea7630bc4327be236ec1bd8)
FetchContent_MakeAvailable(endian char_classify)

# --- Library target ---
add_library(cuuid STATIC cuuid_common.cc cuuid_v1.cc cuuid_v6.cc)
target_include_directories(cuuid PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(cuuid PUBLIC cxx_std_20)
target_link_libraries(cuuid PUBLIC endian::endian char_classify::char_classify)
add_library(cuuid::cuuid ALIAS cuuid)

function(cuuid_add_uuid_include_dir)
	if(UUID_INCLUDE_DIR
		AND NOT UUID_INCLUDE_DIR IN_LIST CMAKE_CXX_IMPLICIT_INCLUDE_DIRECTORIES
		AND NOT UUID_INCLUDE_DIR MATCHES "/SDKs/[^/]+\\.sdk/usr/include$")
		target_include_directories(cuuid PUBLIC "${UUID_INCLUDE_DIR}")
	endif()
endfunction()

# --- Platform UUID backend ---
if(CMAKE_SYSTEM_NAME STREQUAL "FreeBSD")
	find_path(UUID_INCLUDE_DIR uuid.h REQUIRED)
	cuuid_add_uuid_include_dir()
	target_compile_definitions(cuuid PUBLIC UUID_FREEBSD)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin" OR CMAKE_SYSTEM_NAME STREQUAL "Linux")
	if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
		set(CUUID_MACOS_SDK "${CMAKE_OSX_SYSROOT}")
		if(NOT CUUID_MACOS_SDK)
			execute_process(
				COMMAND xcrun --sdk macosx --show-sdk-path
				OUTPUT_VARIABLE CUUID_MACOS_SDK
				OUTPUT_STRIP_TRAILING_WHITESPACE
			)
		endif()
		find_path(UUID_INCLUDE_DIR uuid/uuid.h
			PATHS
				"${CUUID_MACOS_SDK}/usr/include"
				/usr/local/include
				/opt/homebrew/include
				/opt/local/include
			NO_DEFAULT_PATH
			REQUIRED)
	else()
		find_path(UUID_INCLUDE_DIR uuid/uuid.h REQUIRED)
	endif()
	cuuid_add_uuid_include_dir()
	target_compile_definitions(cuuid PUBLIC UUID_LIBUUID)
	if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
		find_library(UUID_LIBRARY NAMES uuid REQUIRED)
		target_link_libraries(cuuid PUBLIC "${UUID_LIBRARY}")
	endif()
elseif(WIN32)
	target_compile_definitions(cuuid PUBLIC UUID_WINDOWS)
	target_link_libraries(cuuid PUBLIC ole32)
else()
	message(FATAL_ERROR "cuuid does not know which UUID backend to use on ${CMAKE_SYSTEM_NAME}")
endif()

# Build tests, example, and benchmark only when this repo is the top-level project.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
	enable_testing()

	add_executable(cuuid_test test/test.cc)
	target_link_libraries(cuuid_test PRIVATE cuuid)
	add_test(NAME cuuid COMMAND cuuid_test)

	add_executable(cuuid_v6_test test/test_v6.cc)
	target_link_libraries(cuuid_v6_test PRIVATE cuuid)
	add_test(NAME cuuid_v6 COMMAND cuuid_v6_test)

	add_executable(cuuid_mt_test test/test_mt.cc)
	target_link_libraries(cuuid_mt_test PRIVATE cuuid)
	add_test(NAME cuuid_mt COMMAND cuuid_mt_test)

	find_package(Threads REQUIRED)
	add_executable(cuuid_v6_threads_test test/test_v6_threads.cc)
	target_link_libraries(cuuid_v6_threads_test PRIVATE cuuid Threads::Threads)
	add_test(NAME cuuid_v6_threads COMMAND cuuid_v6_threads_test)

	add_executable(cuuid_demo examples/demo.cc)
	target_link_libraries(cuuid_demo PRIVATE cuuid)

	add_executable(cuuid_benchmark benchmarks/serialise_parse.cc)
	target_link_libraries(cuuid_benchmark PRIVATE cuuid)

	add_executable(cuuid_v1_bench benchmarks/v1_bench.cc)
	target_link_libraries(cuuid_v1_bench PRIVATE cuuid)

	add_executable(cuuid_compare benchmarks/compare.cc)
	target_link_libraries(cuuid_compare PRIVATE cuuid)

	# Boost.UUID rows in cuuid_compare (text parse, text form, generation):
	#   cmake -B build -DCMAKE_BUILD_TYPE=Release -DCUUID_BENCH_BOOST=ON
	# Boost is found on the system and linked by this target only; its headers are enough.
	option(CUUID_BENCH_BOOST "Add Boost.UUID rows to cuuid_compare" OFF)
	if(CUUID_BENCH_BOOST)
		find_package(Boost QUIET)
		if(Boost_FOUND)
			if(TARGET Boost::headers)
				target_link_libraries(cuuid_compare PRIVATE Boost::headers)
			else()
				target_include_directories(cuuid_compare PRIVATE ${Boost_INCLUDE_DIRS})
			endif()
			target_compile_definitions(cuuid_compare PRIVATE CUUID_BENCH_BOOST)
		else()
			message(WARNING "cuuid_compare: Boost not found, the Boost.UUID rows are skipped")
		endif()
	endif()

	add_executable(cuuid_v2_demo prototype/v2_demo.cc)
	target_link_libraries(cuuid_v2_demo PRIVATE cuuid)
endif()
