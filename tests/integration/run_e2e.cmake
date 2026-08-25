foreach(required SDKCC SPEC SOURCE_DIR PROJECT_DIR WORK GENERATOR
                 C_COMPILER CXX_COMPILER ASAN UBSAN TSAN)
  if(NOT DEFINED ${required})
    message(FATAL_ERROR "${required} is required")
  endif()
endforeach()
file(REMOVE_RECURSE "${WORK}")
set(generated "${WORK}/generated")
set(build "${WORK}/build")
execute_process(
  COMMAND "${SDKCC}" compile "${SPEC}" --output "${generated}"
          --library minimal --namespace minimal --lang c,cpp
  RESULT_VARIABLE generate_result OUTPUT_VARIABLE generate_stdout
  ERROR_VARIABLE generate_stderr
)
if(NOT generate_result EQUAL 0)
  message(FATAL_ERROR "generation failed:\n${generate_stdout}\n${generate_stderr}")
endif()
execute_process(
  COMMAND "${CMAKE_COMMAND}" -S "${PROJECT_DIR}" -B "${build}" -G "${GENERATOR}"
          "-DSDKCC_SOURCE_DIR=${SOURCE_DIR}"
          "-DSDKCC_GENERATED_DIR=${generated}"
          "-DCMAKE_C_COMPILER=${C_COMPILER}"
          "-DCMAKE_CXX_COMPILER=${CXX_COMPILER}"
          "-DSDKCC_ENABLE_ASAN=${ASAN}"
          "-DSDKCC_ENABLE_UBSAN=${UBSAN}"
          "-DSDKCC_ENABLE_TSAN=${TSAN}"
  RESULT_VARIABLE configure_result OUTPUT_VARIABLE configure_stdout
  ERROR_VARIABLE configure_stderr
)
if(NOT configure_result EQUAL 0)
  message(FATAL_ERROR
          "nested configure failed:\n${configure_stdout}\n${configure_stderr}")
endif()
execute_process(
  COMMAND "${CMAKE_COMMAND}" --build "${build}"
  RESULT_VARIABLE build_result OUTPUT_VARIABLE build_stdout
  ERROR_VARIABLE build_stderr
)
if(NOT build_result EQUAL 0)
  message(FATAL_ERROR "nested build failed:\n${build_stdout}\n${build_stderr}")
endif()
execute_process(
  COMMAND "${CMAKE_CTEST_COMMAND}" --test-dir "${build}" --output-on-failure
  RESULT_VARIABLE test_result OUTPUT_VARIABLE test_stdout
  ERROR_VARIABLE test_stderr
)
if(NOT test_result EQUAL 0)
  message(FATAL_ERROR "nested tests failed:\n${test_stdout}\n${test_stderr}")
endif()
