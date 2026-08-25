cmake_minimum_required(VERSION 3.28)

project(sdkcc VERSION 0.1.0 LANGUAGES C CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

option(SDKCC_BUILD_COMPILER "Build the sdkcc compiler" ON)
option(SDKCC_BUILD_TESTS "Build sdkcc tests" ${PROJECT_IS_TOP_LEVEL})
option(SDKCC_BUILD_BENCHMARKS "Build sdkcc benchmarks" ${PROJECT_IS_TOP_LEVEL})
option(SDKCC_BUILD_FUZZERS "Build libFuzzer targets" OFF)
option(SDKCC_ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(SDKCC_ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)
option(SDKCC_ENABLE_TSAN "Enable ThreadSanitizer" OFF)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_C_VISIBILITY_PRESET hidden)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN YES)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(SdkccWarnings)
include(SdkccSanitizers)

file(GLOB_RECURSE SDKCC_FORMAT_FILES CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/compiler/*.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/compiler/*.hpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/runtime/*.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/runtime/*.h"
  "${CMAKE_CURRENT_SOURCE_DIR}/include/*.hpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/*.cpp"
)
find_program(SDKCC_CLANG_FORMAT NAMES clang-format)
if(SDKCC_CLANG_FORMAT)
  add_custom_target(format
    COMMAND "${SDKCC_CLANG_FORMAT}" -i ${SDKCC_FORMAT_FILES}
    COMMENT "Formatting sdkcc sources"
    VERBATIM
  )
endif()
find_program(SDKCC_CLANG_TIDY NAMES clang-tidy)
if(SDKCC_CLANG_TIDY)
  file(GLOB_RECURSE SDKCC_TIDY_FILES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/compiler/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/runtime/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/*.cpp"
  )
  list(APPEND SDKCC_TIDY_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/runtime/runtime_test.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/compiler/compiler_test.cpp"
  )
  add_custom_target(tidy
    COMMAND "${SDKCC_CLANG_TIDY}" -p "${CMAKE_BINARY_DIR}"
            ${SDKCC_TIDY_FILES}
    COMMENT "Running clang-tidy"
    VERBATIM
  )
endif()

add_library(sdkcc_runtime
  runtime/src/allocator.c
  runtime/src/buffer.c
  runtime/src/json.c
  runtime/src/request.c
  runtime/src/test_transport.c
)
add_library(sdkcc::runtime ALIAS sdkcc_runtime)
target_include_directories(sdkcc_runtime
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/runtime/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_compile_definitions(sdkcc_runtime PRIVATE SDKCC_BUILDING_RUNTIME=1)
if(NOT BUILD_SHARED_LIBS)
  target_compile_definitions(sdkcc_runtime PUBLIC SDKCC_STATIC=1)
endif()
target_link_libraries(sdkcc_runtime PRIVATE
  $<BUILD_INTERFACE:sdkcc_warnings>
  $<BUILD_INTERFACE:sdkcc_sanitizers>
)
set_target_properties(sdkcc_runtime PROPERTIES
  EXPORT_NAME runtime
  OUTPUT_NAME sdkcc_runtime
  VERSION ${PROJECT_VERSION}
  SOVERSION 1
  POSITION_INDEPENDENT_CODE ON
)

add_library(sdkcc_cpp INTERFACE)
add_library(sdkcc::cpp ALIAS sdkcc_cpp)
target_include_directories(sdkcc_cpp INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_link_libraries(sdkcc_cpp INTERFACE sdkcc::runtime)
target_compile_features(sdkcc_cpp INTERFACE cxx_std_23)
set_target_properties(sdkcc_cpp PROPERTIES EXPORT_NAME cpp)

if(SDKCC_BUILD_COMPILER)
  find_package(PkgConfig QUIET)
  if(PkgConfig_FOUND)
    pkg_check_modules(LIBYAML QUIET IMPORTED_TARGET yaml-0.1)
    pkg_check_modules(SIMDJSON QUIET IMPORTED_TARGET simdjson)
  endif()
  if(NOT TARGET PkgConfig::LIBYAML)
    find_path(LIBYAML_INCLUDE_DIR NAMES yaml.h REQUIRED)
    find_library(LIBYAML_LIBRARY NAMES yaml yaml_static libyaml REQUIRED)
    add_library(sdkcc_libyaml UNKNOWN IMPORTED)
    set_target_properties(sdkcc_libyaml PROPERTIES
      IMPORTED_LOCATION "${LIBYAML_LIBRARY}"
      INTERFACE_INCLUDE_DIRECTORIES "${LIBYAML_INCLUDE_DIR}"
    )
    add_library(PkgConfig::LIBYAML ALIAS sdkcc_libyaml)
    set(LIBYAML_INCLUDE_DIRS "${LIBYAML_INCLUDE_DIR}")
  endif()
  if(NOT TARGET PkgConfig::SIMDJSON)
    find_path(SIMDJSON_INCLUDE_DIR NAMES simdjson.h REQUIRED)
    find_library(SIMDJSON_LIBRARY NAMES simdjson simdjson_static REQUIRED)
    add_library(sdkcc_simdjson UNKNOWN IMPORTED)
    set_target_properties(sdkcc_simdjson PROPERTIES
      IMPORTED_LOCATION "${SIMDJSON_LIBRARY}"
      INTERFACE_INCLUDE_DIRECTORIES "${SIMDJSON_INCLUDE_DIR}"
    )
    add_library(PkgConfig::SIMDJSON ALIAS sdkcc_simdjson)
    set(SIMDJSON_INCLUDE_DIRS "${SIMDJSON_INCLUDE_DIR}")
  endif()

  add_library(sdkcc_compiler
    compiler/source/document.cpp
    compiler/source/string_interner.cpp
    compiler/diagnostics/diagnostic.cpp
    compiler/frontend/openapi/parser.cpp
    compiler/frontend/openapi/lower.cpp
    compiler/ir/nair.cpp
    compiler/naming/naming.cpp
    compiler/passes/prepare_codegen.cpp
    compiler/backend/code_writer.cpp
    compiler/backend/c/generator.cpp
    compiler/backend/cpp/generator.cpp
    compiler/driver.cpp
  )
  add_library(sdkcc::compiler ALIAS sdkcc_compiler)
  target_include_directories(sdkcc_compiler
    PUBLIC
      $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/compiler/include>
      $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
  )
  target_link_libraries(sdkcc_compiler
    PRIVATE
      PkgConfig::LIBYAML
      PkgConfig::SIMDJSON
      sdkcc_warnings
      sdkcc_sanitizers
  )
  target_include_directories(sdkcc_compiler SYSTEM PRIVATE
    ${LIBYAML_INCLUDE_DIRS}
    ${SIMDJSON_INCLUDE_DIRS}
  )
  target_compile_features(sdkcc_compiler PUBLIC cxx_std_23)

  add_executable(sdkcc compiler/cli/main.cpp)
  target_link_libraries(sdkcc PRIVATE sdkcc::compiler sdkcc_warnings sdkcc_sanitizers)
endif()

if(SDKCC_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

if(SDKCC_BUILD_BENCHMARKS AND SDKCC_BUILD_COMPILER)
  add_subdirectory(benchmarks)
endif()

if(SDKCC_BUILD_FUZZERS)
  if(NOT SDKCC_BUILD_COMPILER)
    message(FATAL_ERROR "SDKCC_BUILD_FUZZERS requires SDKCC_BUILD_COMPILER")
  endif()
  if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "sdkcc fuzz targets require a Clang compiler")
  endif()
  add_subdirectory(tests/fuzz)
endif()

install(TARGETS sdkcc_runtime sdkcc_cpp
  EXPORT sdkccTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
if(SDKCC_BUILD_COMPILER)
  install(TARGETS sdkcc
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  )
endif()
install(DIRECTORY runtime/include/ include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT sdkccTargets
  FILE sdkccTargets.cmake
  NAMESPACE sdkcc::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/sdkcc
)
configure_package_config_file(
  cmake/sdkccConfig.cmake.in
  "${CMAKE_CURRENT_BINARY_DIR}/sdkccConfig.cmake"
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/sdkcc
)
write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/sdkccConfigVersion.cmake"
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion
)
install(FILES
  "${CMAKE_CURRENT_BINARY_DIR}/sdkccConfig.cmake"
  "${CMAKE_CURRENT_BINARY_DIR}/sdkccConfigVersion.cmake"
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/sdkcc
)
