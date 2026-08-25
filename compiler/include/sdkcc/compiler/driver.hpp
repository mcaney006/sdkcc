#ifndef SDKCC_COMPILER_DRIVER_HPP
#define SDKCC_COMPILER_DRIVER_HPP

#include <sdkcc/compiler/diagnostics/diagnostic.hpp>

#include <cstddef>
#include <expected>
#include <filesystem>
#include <string>

namespace sdkcc::compiler {

struct CompileOptions {
  std::filesystem::path input;
  std::filesystem::path output;
  std::string library;
  std::string cpp_namespace;
  bool emit_c{true};
  bool emit_cpp{true};
  bool emit_ir{false};
};

struct CompileStats {
  std::size_t schemas{};
  std::size_t endpoints{};
  std::size_t files{};
  std::filesystem::path output;
};

[[nodiscard]] std::expected<CompileStats, bool>
compile(const CompileOptions &options, DiagnosticBag &diagnostics);

} // namespace sdkcc::compiler

#endif
