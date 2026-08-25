cmake_minimum_required(VERSION 3.28)
project(sdkcc_generated_e2e LANGUAGES C CXX)

if(NOT DEFINED SDKCC_SOURCE_DIR OR NOT DEFINED SDKCC_GENERATED_DIR)
  message(FATAL_ERROR "SDKCC_SOURCE_DIR and SDKCC_GENERATED_DIR are required")
endif()
set(SDKCC_BUILD_COMPILER OFF CACHE BOOL "" FORCE)
set(SDKCC_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SDKCC_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
add_subdirectory("${SDKCC_SOURCE_DIR}" sdkcc-runtime)
add_subdirectory("${SDKCC_GENERATED_DIR}" generated-sdk)

enable_testing()
add_executable(generated_c_test generated_c_test.c)
target_link_libraries(generated_c_test PRIVATE minimal::minimal sdkcc_warnings
                                               sdkcc_sanitizers)
add_test(NAME generated_c_test COMMAND generated_c_test)
add_executable(generated_cpp_test generated_cpp_test.cpp)
target_link_libraries(generated_cpp_test PRIVATE minimal::cpp sdkcc_warnings
                                                   sdkcc_sanitizers)
add_test(NAME generated_cpp_test COMMAND generated_cpp_test)

