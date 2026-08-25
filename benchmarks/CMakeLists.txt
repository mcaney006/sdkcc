add_executable(sdkcc_bench_interner interner_bench.cpp)
target_link_libraries(sdkcc_bench_interner PRIVATE sdkcc::compiler
                                                     sdkcc_warnings)
add_custom_target(bench
  COMMAND sdkcc_bench_interner
  DEPENDS sdkcc_bench_interner
  USES_TERMINAL
)
