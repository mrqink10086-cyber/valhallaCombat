cmake_minimum_required(VERSION 3.22)
project(ValhallaCombatPerf LANGUAGES CXX)

find_package(Python3 COMPONENTS Interpreter REQUIRED)

set(VALHALLA_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")
set(VALHALLA_HOST "${VALHALLA_ROOT}/tests/host")
set(PERF_HOST "${CMAKE_CURRENT_BINARY_DIR}/host")
file(MAKE_DIRECTORY "${PERF_HOST}/include")
file(WRITE "${PERF_HOST}/include/offsets.h" "#pragma once\nnamespace RE::Offset { inline float speed = 1.0f; inline float getSGTM() { return speed; } inline void SGTM(float value) { speed = value; } }\n")
set(PERF_BASELINE "${CMAKE_CURRENT_BINARY_DIR}/baseline")
file(MAKE_DIRECTORY "${PERF_BASELINE}/include")
execute_process(COMMAND git show HEAD:src/bin/CombatRuntime.cpp
	OUTPUT_FILE "${PERF_BASELINE}/CombatRuntime.cpp" WORKING_DIRECTORY "${VALHALLA_ROOT}" COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND git show HEAD:src/include/CombatRuntime.h
	OUTPUT_FILE "${PERF_BASELINE}/include/CombatRuntime.h" WORKING_DIRECTORY "${VALHALLA_ROOT}" COMMAND_ERROR_IS_FATAL ANY)
file(WRITE "${PERF_BASELINE}/include/offsets.h" "#pragma once\nnamespace RE::Offset { inline float speed = 1.0f; inline float getSGTM() { return speed; } inline void SGTM(float value) { speed = value; } }\n")

function(configure_perf_target target)
	target_compile_features(${target} PRIVATE cxx_std_20)
	target_compile_definitions(${target} PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN NDEBUG)
	if(MSVC)
		target_compile_options(${target} PRIVATE /O2 /W4 /WX /utf-8)
	else()
		target_compile_options(${target} PRIVATE -O3 -Wall -Wextra -Werror)
	endif()
endfunction()

set(PERF_FUNCTIONS "${CMAKE_CURRENT_BINARY_DIR}/functions")
set(PERF_UTILS_FUNCTIONS "${PERF_FUNCTIONS}/utils_functions.inc")
add_custom_command(OUTPUT "${PERF_UTILS_FUNCTIONS}"
	COMMAND "${CMAKE_COMMAND}" -E make_directory "${PERF_FUNCTIONS}"
	COMMAND "${Python3_EXECUTABLE}" "${VALHALLA_ROOT}/tests/generate_functions.py" "${VALHALLA_ROOT}" "${PERF_FUNCTIONS}"
	DEPENDS
		"${VALHALLA_ROOT}/tests/generate_functions.py"
		"${VALHALLA_ROOT}/src/bin/settings.cpp"
		"${VALHALLA_ROOT}/src/bin/blockHandler.cpp"
		"${VALHALLA_ROOT}/src/bin/executionHandler.cpp"
		"${VALHALLA_ROOT}/src/bin/debuffHandler.cpp"
		"${VALHALLA_ROOT}/src/bin/OwnedEffects.cpp"
		"${VALHALLA_ROOT}/src/bin/main.cpp"
		"${VALHALLA_ROOT}/src/bin/events/animEventHandler.cpp"
		"${VALHALLA_ROOT}/src/bin/attackHandler.cpp"
		"${VALHALLA_ROOT}/src/bin/staminaHandler.cpp"
		"${VALHALLA_ROOT}/src/bin/Hooks.cpp"
		"${VALHALLA_ROOT}/src/bin/hitProcessor.cpp"
		"${VALHALLA_ROOT}/src/bin/stunHandler.cpp"
		"${VALHALLA_ROOT}/src/include/attackHandler.h"
		"${VALHALLA_ROOT}/src/include/Utils.cpp"
		"${VALHALLA_ROOT}/src/include/Utils.h"
	VERBATIM)
add_custom_target(perf_functions DEPENDS "${PERF_UTILS_FUNCTIONS}")

add_executable(runtime_perf
	"${VALHALLA_ROOT}/tests/perf_runtime.cpp"
	"${VALHALLA_ROOT}/src/bin/CombatRuntime.cpp")
target_include_directories(runtime_perf BEFORE PRIVATE "${PERF_HOST}" "${VALHALLA_ROOT}/src" "${VALHALLA_HOST}")
configure_perf_target(runtime_perf)
if(MSVC)
	target_compile_options(runtime_perf PRIVATE "/FI${VALHALLA_HOST}/RuntimeHost.h")
else()
	target_compile_options(runtime_perf PRIVATE "-include${VALHALLA_HOST}/RuntimeHost.h")
endif()

add_executable(runtime_perf_baseline
	"${VALHALLA_ROOT}/tests/perf_runtime.cpp"
	"${PERF_BASELINE}/CombatRuntime.cpp")
target_include_directories(runtime_perf_baseline BEFORE PRIVATE "${PERF_BASELINE}" "${VALHALLA_ROOT}/src" "${VALHALLA_HOST}")
configure_perf_target(runtime_perf_baseline)
if(MSVC)
	target_compile_options(runtime_perf_baseline PRIVATE "/FI${VALHALLA_HOST}/RuntimeHost.h")
else()
	target_compile_options(runtime_perf_baseline PRIVATE "-include${VALHALLA_HOST}/RuntimeHost.h")
endif()

add_executable(rotation_perf
	"${VALHALLA_ROOT}/tests/perf_rotation.cpp"
	"${VALHALLA_ROOT}/tests/perf_rotation_avx2.cpp"
	"${PERF_UTILS_FUNCTIONS}")
add_dependencies(rotation_perf perf_functions)
target_include_directories(rotation_perf PRIVATE "${VALHALLA_ROOT}/tests" "${PERF_FUNCTIONS}")
configure_perf_target(rotation_perf)
if(MSVC)
	set_source_files_properties("${VALHALLA_ROOT}/tests/perf_rotation_avx2.cpp" PROPERTIES COMPILE_OPTIONS "/arch:AVX2")
else()
	set_source_files_properties("${VALHALLA_ROOT}/tests/perf_rotation_avx2.cpp" PROPERTIES COMPILE_OPTIONS "-mavx2")
endif()
