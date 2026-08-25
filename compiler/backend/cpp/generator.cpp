#include <sdkcc/compiler/backend/cpp/generator.hpp>

#include <cctype>
#include <filesystem>
#include <set>
#include <stdexcept>
#include <string>
#include <variant>

namespace sdkcc::compiler::backend::cpp {
namespace {

std::string upper(std::string_view input) {
  std::string result;
  result.reserve(input.size());
  for (const char source_byte : input) {
    result.push_back(static_cast<char>(
        std::toupper(static_cast<unsigned char>(source_byte))));
  }
  return result;
}

std::string c_model(const CodegenPlan &plan, TypeId type) {
  return plan.library + '_' + std::string{plan.type_name(type)} + "_t";
}

std::string c_input(const CodegenPlan &plan, TypeId type) {
  return plan.library + '_' + std::string{plan.type_name(type)} + "_input_t";
}

std::string c_endpoint(const CodegenPlan &plan, EndpointId endpoint) {
  return plan.library + '_' + std::string{plan.endpoint_name(endpoint)};
}

const nair::PrimitiveType &primitive(const nair::Module &module, TypeId type) {
  const auto *result =
      std::get_if<nair::PrimitiveType>(&module.type(type).body);
  if (result == nullptr) {
    throw std::logic_error{"Milestone 1 field is not primitive"};
  }
  return *result;
}

std::string cpp_type(const nair::Module &module, TypeId type, bool view) {
  switch (primitive(module, type).kind) {
  case nair::PrimitiveKind::string:
    return view ? "std::string_view" : "std::string";
  case nair::PrimitiveKind::integer:
    return "std::int64_t";
  case nair::PrimitiveKind::boolean:
    return "bool";
  }
  throw std::logic_error{"unknown primitive kind"};
}

std::set<TypeId> request_types(const nair::Module &module) {
  std::set<TypeId> result;
  for (const auto &endpoint : module.endpoints()) {
    if (endpoint.request_body) {
      result.emplace(endpoint.request_body->type);
    }
  }
  return result;
}

void emit_cpp_model(CodeWriter &writer, const nair::Module &module,
                    const CodegenPlan &plan, TypeId type_id, bool input) {
  const auto &structure = std::get<nair::StructType>(module.type(type_id).body);
  const std::string name =
      std::string{plan.type_name(type_id)} + (input ? "_input" : "");
  writer.line("struct " + name + " {");
  writer.indent();
  for (std::size_t index = 0U; index < structure.fields.size(); ++index) {
    const auto &field = structure.fields[index];
    auto type = cpp_type(module, field.type, input);
    if (!field.required) {
      type = "std::optional<" + type + '>';
    }
    writer.line(type + ' ' + std::string{plan.field_name(type_id, index)} +
                "{};");
  }
  writer.dedent();
  writer.line("};");
  writer.line();
}

std::string c_view(std::string expression) {
  return "sdkcc_string_view_t{.data = " + expression +
         ".data(), .len = " + expression + ".size()}";
}

void emit_from_c(CodeWriter &writer, const nair::Module &module,
                 const CodegenPlan &plan, TypeId type_id) {
  const auto &structure = std::get<nair::StructType>(module.type(type_id).body);
  const auto name = std::string{plan.type_name(type_id)};
  writer.line("[[nodiscard]] inline " + name + " from_c(const " +
              c_model(plan, type_id) + " &source) {");
  writer.indent();
  writer.line("return " + name + "{");
  writer.indent();
  for (std::size_t index = 0U; index < structure.fields.size(); ++index) {
    const auto &field = structure.fields[index];
    const auto field_name = std::string{plan.field_name(type_id, index)};
    std::string value;
    switch (primitive(module, field.type).kind) {
    case nair::PrimitiveKind::string:
      value =
          "sdkcc::copy(sdkcc_v1_owned_string_view(&source." + field_name + "))";
      break;
    case nair::PrimitiveKind::integer:
    case nair::PrimitiveKind::boolean:
      value = "source." + field_name;
      break;
    }
    if (!field.required) {
      value = "source.has_" + field_name + " ? std::optional{" + value +
              "} : std::nullopt";
    }
    writer.line("." + field_name + " = " + value + ",");
  }
  writer.dedent();
  writer.line("};");
  writer.dedent();
  writer.line("}");
  writer.line();
}

std::string argument_type(const nair::Module &module,
                          const nair::Parameter &parameter) {
  auto type = cpp_type(module, parameter.type, true);
  if (!parameter.required) {
    type = "std::optional<" + type + ">";
  }
  return type;
}

void emit_input_conversion(CodeWriter &writer, const nair::Module &module,
                           const CodegenPlan &plan, TypeId type_id) {
  const auto &structure = std::get<nair::StructType>(module.type(type_id).body);
  writer.line(c_input(plan, type_id) + " c_body{};");
  for (std::size_t index = 0U; index < structure.fields.size(); ++index) {
    const auto &field = structure.fields[index];
    const auto name = std::string{plan.field_name(type_id, index)};
    if (!field.required) {
      writer.line("if (body." + name + ") {");
      writer.indent();
      writer.line("c_body.has_" + name + " = true;");
    }
    const std::string source =
        "body." + name + (field.required ? "" : ".value()");
    if (primitive(module, field.type).kind == nair::PrimitiveKind::string) {
      writer.line("c_body." + name + " = " + c_view(source) + ";");
    } else {
      writer.line("c_body." + name + " = " + source + ";");
    }
    if (!field.required) {
      writer.dedent();
      writer.line("}");
    }
  }
  writer.line("c_params.body = &c_body;");
}

void emit_endpoint_method(CodeWriter &writer, const nair::Module &module,
                          const CodegenPlan &plan, EndpointId endpoint_id) {
  const auto &endpoint = module.endpoint(endpoint_id);
  const auto function = std::string{plan.endpoint_name(endpoint_id)};
  const auto result_type = std::string{plan.type_name(endpoint.response.type)};
  std::string signature = "[[nodiscard]] std::expected<" + result_type +
                          ", sdkcc::error> " + function + "(";
  bool first = true;
  for (std::size_t index = 0U; index < endpoint.parameters.size(); ++index) {
    if (!first) {
      signature += ", ";
    }
    signature += argument_type(module, endpoint.parameters[index]) + " p_" +
                 std::string{plan.parameter_name(endpoint_id, index)};
    first = false;
  }
  if (endpoint.request_body) {
    if (!first) {
      signature += ", ";
    }
    signature += "const " +
                 std::string{plan.type_name(endpoint.request_body->type)} +
                 "_input &body";
  }
  signature += ") {";
  writer.line(signature);
  writer.indent();
  writer.line(c_endpoint(plan, endpoint_id) + "_params_t c_params{};");
  for (std::size_t index = 0U; index < endpoint.parameters.size(); ++index) {
    const auto &parameter = endpoint.parameters[index];
    const auto name = std::string{plan.parameter_name(endpoint_id, index)};
    const std::string source =
        "p_" + name + (parameter.required ? "" : ".value()");
    if (!parameter.required) {
      writer.line("if (p_" + name + ") {");
      writer.indent();
      writer.line("c_params.has_" + name + " = true;");
    }
    if (primitive(module, parameter.type).kind == nair::PrimitiveKind::string) {
      writer.line("c_params." + name + " = " + c_view(source) + ";");
    } else {
      writer.line("c_params." + name + " = " + source + ";");
    }
    if (!parameter.required) {
      writer.dedent();
      writer.line("}");
    }
  }
  if (endpoint.request_body) {
    emit_input_conversion(writer, module, plan, endpoint.request_body->type);
  }
  writer.line(c_endpoint(plan, endpoint_id) + "_response_t c_response{};");
  writer.line("auto reset_response = [](" + c_endpoint(plan, endpoint_id) +
              "_response_t *value) {");
  writer.indent();
  writer.line(plan.library + "_v1_model_" + result_type + "_reset(value);");
  writer.dedent();
  writer.line("};");
  writer.line("std::unique_ptr<" + c_endpoint(plan, endpoint_id) +
              "_response_t, decltype(reset_response)> response_guard{"
              "&c_response, reset_response};");
  writer.line("sdkcc::error_slot error;");
  writer.line("const auto status = " + plan.library + "_v1_" + function +
              "(handle_, &c_params, &c_response, error.get());");
  writer.line("if (status != SDKCC_OK) {");
  writer.indent();
  writer.line("return std::unexpected{error.copy()};");
  writer.dedent();
  writer.line("}");
  writer.line("return detail::from_c(c_response);");
  writer.dedent();
  writer.line("}");
  writer.line();
}

std::string generate_header(const nair::Module &module,
                            const CodegenPlan &plan) {
  CodeWriter writer;
  writer.line("/* Generated by sdkcc. Do not edit. */");
  writer.line("#ifndef " + upper(plan.library) + "_SDK_HPP");
  writer.line("#define " + upper(plan.library) + "_SDK_HPP");
  writer.line();
  writer.line("#include <" + plan.library + "/" + plan.library + ".h>");
  writer.line("#include <sdkcc/sdkcc.hpp>");
  writer.line();
  writer.line("#include <cstdint>");
  writer.line("#include <expected>");
  writer.line("#include <memory>");
  writer.line("#include <optional>");
  writer.line("#include <string>");
  writer.line("#include <string_view>");
  writer.line("#include <utility>");
  writer.line();
  writer.line("namespace " + plan.cpp_namespace + " {");
  writer.line();
  for (const TypeId type : plan.emitted_types) {
    emit_cpp_model(writer, module, plan, type, false);
  }
  for (const TypeId type : request_types(module)) {
    emit_cpp_model(writer, module, plan, type, true);
  }
  writer.line("namespace detail {");
  writer.line();
  std::set<TypeId> converted;
  for (const auto &endpoint : module.endpoints()) {
    converted.emplace(endpoint.response.type);
  }
  for (const TypeId type : converted) {
    emit_from_c(writer, module, plan, type);
  }
  writer.line("} // namespace detail");
  writer.line();
  writer.line("struct config {");
  writer.indent();
  writer.line("std::string_view base_url{};");
  if (module.api_key_auth) {
    writer.line("std::string_view api_key{};");
  }
  writer.line("sdkcc_allocator_t allocator{};");
  writer.line("sdkcc_transport_t transport{};");
  writer.dedent();
  writer.line("};");
  writer.line();
  writer.line("class client final {");
  writer.line("public:");
  writer.indent();
  writer.line("client(const client &) = delete;");
  writer.line("client &operator=(const client &) = delete;");
  writer.line("client(client &&other) noexcept "
              ": handle_{std::exchange(other.handle_, nullptr)} {}");
  writer.line("client &operator=(client &&other) noexcept {");
  writer.indent();
  writer.line("if (this != &other) {");
  writer.indent();
  writer.line(plan.library + "_v1_client_destroy(handle_);");
  writer.line("handle_ = std::exchange(other.handle_, nullptr);");
  writer.dedent();
  writer.line("}");
  writer.line("return *this;");
  writer.dedent();
  writer.line("}");
  writer.line("~client() { " + plan.library + "_v1_client_destroy(handle_); }");
  writer.line();
  writer.line("[[nodiscard]] static std::expected<client, sdkcc::error> "
              "create(const config &value) {");
  writer.indent();
  writer.line(plan.library + "_config_t c_config{");
  writer.indent();
  writer.line(".base_url = sdkcc_string_view_t{"
              ".data = value.base_url.data(), .len = value.base_url.size()},");
  if (module.api_key_auth) {
    writer.line(".api_key = sdkcc_string_view_t{"
                ".data = value.api_key.data(), .len = value.api_key.size()},");
  }
  writer.line(".allocator = value.allocator,");
  writer.line(".transport = value.transport,");
  writer.dedent();
  writer.line("};");
  writer.line(plan.library + "_client_t *handle = nullptr;");
  writer.line("sdkcc::error_slot error;");
  writer.line("const auto status = " + plan.library +
              "_v1_client_create(&c_config, &handle, error.get());");
  writer.line("if (status != SDKCC_OK) {");
  writer.indent();
  writer.line("return std::unexpected{error.copy()};");
  writer.dedent();
  writer.line("}");
  writer.line("return client{handle};");
  writer.dedent();
  writer.line("}");
  writer.line();
  for (const EndpointId endpoint : plan.emitted_endpoints) {
    emit_endpoint_method(writer, module, plan, endpoint);
  }
  writer.dedent();
  writer.line("private:");
  writer.indent();
  writer.line("explicit client(" + plan.library +
              "_client_t *handle) noexcept : handle_{handle} {}");
  writer.line(plan.library + "_client_t *handle_{};");
  writer.dedent();
  writer.line("};");
  writer.line();
  writer.line("} // namespace " + plan.cpp_namespace);
  writer.line();
  writer.line("#endif");
  return std::move(writer).take();
}

} // namespace

std::vector<GeneratedFile> generate(const nair::Module &module,
                                    const CodegenPlan &plan) {
  return {{.relative_path = std::filesystem::path{"include"} / plan.library /
                            (plan.library + ".hpp"),
           .content = generate_header(module, plan)}};
}

} // namespace sdkcc::compiler::backend::cpp
