#ifndef SDKCC_COMPILER_FRONTEND_OPENAPI_PARSER_HPP
#define SDKCC_COMPILER_FRONTEND_OPENAPI_PARSER_HPP

#include <sdkcc/compiler/diagnostics/diagnostic.hpp>
#include <sdkcc/compiler/source/document.hpp>

#include <cstddef>
#include <expected>
#include <memory>
#include <span>
#include <string_view>

namespace sdkcc::compiler::openapi {

struct ParseLimits {
  std::size_t max_document_bytes{std::size_t{16U} * std::size_t{1024U} *
                                 std::size_t{1024U}};
  std::size_t max_nodes{1'000'000U};
  std::size_t max_depth{128U};
  std::size_t max_key_bytes{4096U};
};

[[nodiscard]] std::expected<std::unique_ptr<ParsedDocument>, bool>
parse_document(std::string_view source_name, std::span<const std::byte> input,
               DiagnosticBag &diagnostics, ParseLimits limits = {});

} // namespace sdkcc::compiler::openapi

#endif
