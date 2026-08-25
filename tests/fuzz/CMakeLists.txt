function(sdkcc_add_fuzzer target source)
  add_executable(${target} ${source})
  target_compile_options(${target} PRIVATE -fsanitize=fuzzer)
  target_link_options(${target} PRIVATE -fsanitize=fuzzer)
  target_link_libraries(${target} PRIVATE sdkcc_warnings sdkcc_sanitizers
                                          ${ARGN})
endfunction()

sdkcc_add_fuzzer(sdkcc_runtime_json_fuzz runtime_json_fuzz.cpp sdkcc::runtime)
sdkcc_add_fuzzer(sdkcc_openapi_fuzz openapi_fuzz.cpp sdkcc::compiler)
