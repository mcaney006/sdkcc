#include <sdkcc/compiler/ir/nair.hpp>

#include <limits>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace sdkcc::compiler::nair {

Module::Module()
    : arena_{}, strings_{&arena_}, types_{&arena_}, endpoints_{&arena_} {}

TypeId Module::add_primitive(std::string_view type_name, PrimitiveKind kind) {
  if (types_.size() >=
      static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
    throw std::length_error{"NAIR type ID space exhausted"};
  }
  const auto id = TypeId{static_cast<std::uint32_t>(types_.size())};
  types_.push_back(Type{.id = id,
                        .name = intern(type_name),
                        .source = {},
                        .body = PrimitiveType{kind}});
  return id;
}

TypeId Module::add_struct(std::string_view type_name, SourceRange source) {
  if (types_.size() >=
      static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
    throw std::length_error{"NAIR type ID space exhausted"};
  }
  const auto id = TypeId{static_cast<std::uint32_t>(types_.size())};
  types_.push_back(Type{.id = id,
                        .name = intern(type_name),
                        .source = std::move(source),
                        .body = StructType{&arena_}});
  return id;
}

EndpointId Module::add_endpoint(Endpoint endpoint) {
  if (endpoints_.size() >=
      static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
    throw std::length_error{"NAIR endpoint ID space exhausted"};
  }
  const auto id = EndpointId{static_cast<std::uint32_t>(endpoints_.size())};
  endpoint.id = id;
  endpoints_.push_back(std::move(endpoint));
  return id;
}

Type &Module::type_mut(TypeId id) {
  if (!id.valid() || static_cast<std::size_t>(id.value()) >= types_.size()) {
    throw std::out_of_range{"invalid TypeId"};
  }
  return types_[id.value()];
}

const Type &Module::type(TypeId id) const {
  if (!id.valid() || static_cast<std::size_t>(id.value()) >= types_.size()) {
    throw std::out_of_range{"invalid TypeId"};
  }
  return types_[id.value()];
}

Endpoint &Module::endpoint_mut(EndpointId id) {
  if (!id.valid() ||
      static_cast<std::size_t>(id.value()) >= endpoints_.size()) {
    throw std::out_of_range{"invalid EndpointId"};
  }
  return endpoints_[id.value()];
}

const Endpoint &Module::endpoint(EndpointId id) const {
  if (!id.valid() ||
      static_cast<std::size_t>(id.value()) >= endpoints_.size()) {
    throw std::out_of_range{"invalid EndpointId"};
  }
  return endpoints_[id.value()];
}

std::string_view method_name(HttpMethod method) noexcept {
  switch (method) {
  case HttpMethod::get:
    return "GET";
  case HttpMethod::post:
    return "POST";
  }
  return "UNKNOWN";
}

std::string_view primitive_name(PrimitiveKind kind) noexcept {
  switch (kind) {
  case PrimitiveKind::string:
    return "string";
  case PrimitiveKind::integer:
    return "integer";
  case PrimitiveKind::boolean:
    return "boolean";
  }
  return "unknown";
}

std::string dump(const Module &module) {
  std::ostringstream output;
  output << "module " << module.string(module.name) << "\n";
  output << "  base_url: " << module.string(module.base_url) << "\n";
  if (module.api_key_auth) {
    output << "  api_key " << module.string(module.api_key_auth->scheme_name)
           << " header " << module.string(module.api_key_auth->wire_name)
           << "\n";
  }
  output << "types:\n";
  for (const auto &type : module.types()) {
    output << "  t" << type.id.value() << ' ' << module.string(type.name);
    if (const auto *primitive = std::get_if<PrimitiveType>(&type.body)) {
      output << " = " << primitive_name(primitive->kind) << "\n";
      continue;
    }
    output << " = struct\n";
    const auto &structure = std::get<StructType>(type.body);
    for (const auto &field : structure.fields) {
      output << "    " << module.string(field.wire_name) << ": t"
             << field.type.value()
             << (field.required ? " required" : " optional") << "\n";
    }
  }
  output << "endpoints:\n";
  for (const auto &endpoint : module.endpoints()) {
    output << "  e" << endpoint.id.value() << ' '
           << module.string(endpoint.operation_name) << ' '
           << method_name(endpoint.method) << ' '
           << module.string(endpoint.path) << " -> t"
           << endpoint.response.type.value() << "\n";
    for (const auto &parameter : endpoint.parameters) {
      output << "    "
             << (parameter.location == ParameterLocation::path ? "path "
                                                               : "query ")
             << module.string(parameter.wire_name) << ": t"
             << parameter.type.value()
             << (parameter.required ? " required" : " optional") << "\n";
    }
    if (endpoint.request_body) {
      output << "    body: t" << endpoint.request_body->type.value()
             << (endpoint.request_body->required ? " required" : " optional")
             << "\n";
    }
  }
  return output.str();
}

} // namespace sdkcc::compiler::nair
