add_executable(sdkcc_runtime_test runtime/runtime_test.c)
target_link_libraries(sdkcc_runtime_test PRIVATE sdkcc::runtime sdkcc_warnings
                                                 sdkcc_sanitizers)
add_test(NAME runtime.unit COMMAND sdkcc_runtime_test)

if(SDKCC_BUILD_COMPILER)
  add_executable(sdkcc_compiler_test compiler/compiler_test.cpp)
  target_link_libraries(sdkcc_compiler_test PRIVATE sdkcc::compiler
                                                     sdkcc_warnings
                                                     sdkcc_sanitizers)
  target_compile_definitions(
    sdkcc_compiler_test
    PRIVATE SDKCC_MINIMAL_SPEC="${CMAKE_CURRENT_SOURCE_DIR}/specs/minimal.yaml"
  )
  add_test(NAME compiler.unit COMMAND sdkcc_compiler_test)

  add_test(
    NAME compiler.determinism
    COMMAND
      "${CMAKE_COMMAND}"
      "-DSDKCC=$<TARGET_FILE:sdkcc>"
      "-DSPEC=${CMAKE_CURRENT_SOURCE_DIR}/specs/minimal.yaml"
      "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/determinism"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/compiler/determinism.cmake"
  )

  add_test(
    NAME integration.generated_native
    COMMAND
      "${CMAKE_COMMAND}"
      "-DSDKCC=$<TARGET_FILE:sdkcc>"
      "-DSPEC=${CMAKE_CURRENT_SOURCE_DIR}/specs/minimal.yaml"
      "-DSOURCE_DIR=${PROJECT_SOURCE_DIR}"
      "-DPROJECT_DIR=${CMAKE_CURRENT_SOURCE_DIR}/integration"
      "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/generated-e2e"
      "-DGENERATOR=${CMAKE_GENERATOR}"
      "-DC_COMPILER=${CMAKE_C_COMPILER}"
      "-DCXX_COMPILER=${CMAKE_CXX_COMPILER}"
      "-DASAN=${SDKCC_ENABLE_ASAN}"
      "-DUBSAN=${SDKCC_ENABLE_UBSAN}"
      "-DTSAN=${SDKCC_ENABLE_TSAN}"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/integration/run_e2e.cmake"
  )
  set_tests_properties(
    compiler.determinism integration.generated_native
    PROPERTIES RUN_SERIAL TRUE TIMEOUT 180
  )
endif()
