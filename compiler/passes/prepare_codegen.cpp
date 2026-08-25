#include <sdkcc/compiler/passes/prepare_codegen.hpp>

#include <sdkcc/compiler/naming/naming.hpp>

#include <algorithm>
#include <stdexcept>
#include <tuple>
#include <variant>

namespace sdkcc::compiler {

std::string_view CodegenPlan::type_name(TypeId id) const {
  return type_names.at(id.value());
}

std::string_view CodegenPlan::field_name(TypeId id, std::size_t index) const {
  return field_names.at(id.value()).at(index);
}

std::string_view CodegenPlan::endpoint_name(EndpointId id) const {
  return endpoint_names.at(id.value());
}

std::string_view CodegenPlan::parameter_name(EndpointId id,
                                             std::size_t index) const {
  return parameter_names.at(id.value()).at(index);
}

std::expected<CodegenPlan, bool> prepare_codegen(const nair::Module &module,
                                                 std::string_view library,
                                                 std::string_view cpp_namespace,
                                                 DiagnosticBag &diagnostics) {
  if (library.empty() || library.size() > 128U) {
    diagnostics.error("E2001",
                      "library name must contain between 1 and 128 bytes");
    return std::unexpected{false};
  }
  CodegenPlan plan;
  plan.library = canonical_identifier(library, IdentifierLanguage::cpp);
  plan.cpp_namespace = canonical_identifier(
      cpp_namespace.empty() ? library : cpp_namespace, IdentifierLanguage::cpp);
  if (plan.library != library) {
    diagnostics.warning("W2001", "library name \"" + std::string{library} +
                                     "\" was canonicalized to \"" +
                                     plan.library + "\"");
  }

  plan.type_names.resize(module.types().size());
  plan.field_names.resize(module.types().size());
  NameScope type_scope;
  for (const auto &type : module.types()) {
    const auto raw_name = module.string(type.name);
    auto generated = type_scope.claim(raw_name, IdentifierLanguage::cpp);
    plan.type_names[type.id.value()] = std::move(generated);
    if (const auto *structure = std::get_if<nair::StructType>(&type.body)) {
      auto &fields = plan.field_names[type.id.value()];
      fields.reserve(structure->fields.size());
      NameScope field_scope;
      for (const auto &field : structure->fields) {
        fields.push_back(field_scope.claim(module.string(field.wire_name),
                                           IdentifierLanguage::cpp));
      }
      plan.emitted_types.push_back(type.id);
    }
  }
  std::ranges::sort(plan.emitted_types, [&](TypeId left, TypeId right) {
    return plan.type_name(left) < plan.type_name(right);
  });

  plan.endpoint_names.resize(module.endpoints().size());
  plan.parameter_names.resize(module.endpoints().size());
  NameScope endpoint_scope;
  (void)endpoint_scope.claim("client_create", IdentifierLanguage::cpp);
  (void)endpoint_scope.claim("client_destroy", IdentifierLanguage::cpp);
  for (const auto &endpoint : module.endpoints()) {
    const auto raw_name = module.string(endpoint.operation_name);
    auto generated = endpoint_scope.claim(raw_name, IdentifierLanguage::cpp);
    if (generated != raw_name) {
      diagnostics.warning("W2017",
                          "operationId \"" + std::string{raw_name} +
                              "\" is not a unique portable C/C++ identifier",
                          endpoint.source,
                          "generated symbol: " + plan.library + '_' +
                              generated);
    }
    plan.endpoint_names[endpoint.id.value()] = std::move(generated);
    auto &parameters = plan.parameter_names[endpoint.id.value()];
    parameters.reserve(endpoint.parameters.size());
    NameScope parameter_scope;
    for (const auto &parameter : endpoint.parameters) {
      parameters.push_back(parameter_scope.claim(
          module.string(parameter.wire_name), IdentifierLanguage::cpp));
    }
    plan.emitted_endpoints.push_back(endpoint.id);
  }
  std::ranges::sort(plan.emitted_endpoints,
                    [&](EndpointId left, EndpointId right) {
                      const auto &lhs = module.endpoint(left);
                      const auto &rhs = module.endpoint(right);
                      return std::tuple{module.string(lhs.path), lhs.method,
                                        plan.endpoint_name(left)} <
                             std::tuple{module.string(rhs.path), rhs.method,
                                        plan.endpoint_name(right)};
                    });
  return plan;
}

} // namespace sdkcc::compiler
