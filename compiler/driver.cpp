#include <sdkcc/compiler/driver.hpp>

#include <sdkcc/compiler/backend/c/generator.hpp>
#include <sdkcc/compiler/backend/code_writer.hpp>
#include <sdkcc/compiler/backend/cpp/generator.hpp>
#include <sdkcc/compiler/frontend/openapi/lower.hpp>
#include <sdkcc/compiler/frontend/openapi/parser.hpp>
#include <sdkcc/compiler/ir/nair.hpp>
#include <sdkcc/compiler/passes/prepare_codegen.hpp>

#include <algorithm>
#include <cstddef>
#include <fstream>
#include <iterator>
#include <string>
#include <variant>
#include <vector>

namespace sdkcc::compiler {
namespace {

std::expected<std::vector<std::byte>, bool>
read_input(const std::filesystem::path &path, DiagnosticBag &diagnostics,
           std::size_t byte_limit) {
  std::error_code filesystem_error;
  const auto size = std::filesystem::file_size(path, filesystem_error);
  if (filesystem_error) {
    diagnostics.error("E0002", "cannot stat input \"" + path.string() +
                                   "\": " + filesystem_error.message());
    return std::unexpected{false};
  }
  if (size > byte_limit) {
    diagnostics.error("E1000",
                      "API description exceeds the configured byte limit");
    return std::unexpected{false};
  }
  std::ifstream input{path, std::ios::binary};
  if (!input) {
    diagnostics.error("E0003", "cannot open input \"" + path.string() + "\"");
    return std::unexpected{false};
  }
  std::vector<std::byte> bytes(static_cast<std::size_t>(size));
  if (!bytes.empty()) {
    input.read(reinterpret_cast<char *>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
  }
  if (!input && !input.eof()) {
    diagnostics.error("E0004",
                      "failed reading input \"" + path.string() + "\"");
    return std::unexpected{false};
  }
  return bytes;
}

} // namespace

std::expected<CompileStats, bool> compile(const CompileOptions &options,
                                          DiagnosticBag &diagnostics) {
  if (options.input.empty() || options.output.empty()) {
    diagnostics.error("E0001", "input and output paths are required");
    return std::unexpected{false};
  }
  if (!options.emit_c && !options.emit_cpp) {
    diagnostics.error("E0005", "at least one output language is required");
    return std::unexpected{false};
  }
  try {
    const openapi::ParseLimits limits;
    auto bytes =
        read_input(options.input, diagnostics, limits.max_document_bytes);
    if (!bytes) {
      return std::unexpected{false};
    }
    auto document = openapi::parse_document(options.input.string(), *bytes,
                                            diagnostics, limits);
    if (!document) {
      return std::unexpected{false};
    }
    auto module = openapi::lower_to_nair(**document, diagnostics);
    if (!module) {
      return std::unexpected{false};
    }
    const std::string library = options.library.empty()
                                    ? options.input.stem().string()
                                    : options.library;
    auto plan =
        prepare_codegen(**module, library, options.cpp_namespace, diagnostics);
    if (!plan) {
      return std::unexpected{false};
    }

    // The C++ layer is intentionally over the exact C ABI, so requesting C++
    // also emits the C library it wraps.
    std::vector<GeneratedFile> files =
        backend::c::generate(**module, *plan, options.emit_cpp);
    if (options.emit_cpp) {
      auto cpp_files = backend::cpp::generate(**module, *plan);
      files.insert(files.end(), std::make_move_iterator(cpp_files.begin()),
                   std::make_move_iterator(cpp_files.end()));
    }
    if (options.emit_ir) {
      files.push_back(
          {.relative_path = "nair.txt", .content = nair::dump(**module)});
    }
    const std::size_t file_count = files.size();
    if (!write_generated_files(options.output, std::move(files), diagnostics)) {
      return std::unexpected{false};
    }
    const auto schema_count =
        std::ranges::count_if((*module)->types(), [](const nair::Type &type) {
          return std::holds_alternative<nair::StructType>(type.body);
        });
    return CompileStats{
        .schemas = static_cast<std::size_t>(schema_count),
        .endpoints = (*module)->endpoints().size(),
        .files = file_count,
        .output = options.output,
    };
  } catch (const std::bad_alloc &) {
    diagnostics.error("E0098", "memory allocation failed during compilation");
  } catch (const std::exception &exception) {
    diagnostics.error("E0099", std::string{"internal compiler failure: "} +
                                   exception.what());
  }
  return std::unexpected{false};
}

} // namespace sdkcc::compiler
