#ifndef SDKCC_COMPILER_PASSES_PREPARE_CODEGEN_HPP
#define SDKCC_COMPILER_PASSES_PREPARE_CODEGEN_HPP

#include <sdkcc/compiler/diagnostics/diagnostic.hpp>
#include <sdkcc/compiler/ir/nair.hpp>

#include <expected>
#include <string>
#include <string_view>
#include <vector>

namespace sdkcc::compiler {

struct CodegenPlan {
  std::string library;
  std::string cpp_namespace;
  std::vector<std::string> type_names;
  std::vector<std::vector<std::string>> field_names;
  std::vector<std::string> endpoint_names;
  std::vector<std::vector<std::string>> parameter_names;
  std::vector<TypeId> emitted_types;
  std::vector<EndpointId> emitted_endpoints;

  [[nodiscard]] std::string_view type_name(TypeId id) const;
  [[nodiscard]] std::string_view field_name(TypeId id, std::size_t index) const;
  [[nodiscard]] std::string_view endpoint_name(EndpointId id) const;
  [[nodiscard]] std::string_view parameter_name(EndpointId id,
                                                std::size_t index) const;
};

[[nodiscard]] std::expected<CodegenPlan, bool>
prepare_codegen(const nair::Module &module, std::string_view library,
                std::string_view cpp_namespace, DiagnosticBag &diagnostics);

} // namespace sdkcc::compiler

#endif
