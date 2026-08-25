#include <sdkcc/compiler/frontend/openapi/lower.hpp>

#include <algorithm>
#include <charconv>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace sdkcc::compiler::openapi {
namespace {

struct NamedNode {
  std::string_view name;
  NodeId node;
  SourceRange source;
};

struct AuthScheme {
  std::string wire_name;
  SourceRange source;
};

class Lowerer final {
public:
  Lowerer(const ParsedDocument &document, DiagnosticBag &diagnostics)
      : document_{document}, diagnostics_{diagnostics},
        module_{std::make_unique<nair::Module>()} {}

  std::expected<std::unique_ptr<nair::Module>, bool> run() {
    const NodeId root = document_.root();
    if (!expect_kind(root, NodeKind::object, "E1100",
                     "OpenAPI document root must be an object")) {
      return std::unexpected{false};
    }
    if (!parse_version(root) || !parse_metadata(root)) {
      return std::unexpected{false};
    }
    string_type_ =
        module_->add_primitive("string", nair::PrimitiveKind::string);
    integer_type_ =
        module_->add_primitive("integer", nair::PrimitiveKind::integer);
    boolean_type_ =
        module_->add_primitive("boolean", nair::PrimitiveKind::boolean);
    if (!parse_components(root) || !parse_security_schemes(root) ||
        !parse_paths(root)) {
      return std::unexpected{false};
    }
    if (const auto webhooks = document_.find(root, "webhooks");
        webhooks && !document_.members(*webhooks).empty()) {
      diagnostics_.error(
          "E1131", "webhooks are not supported by the Milestone 1 frontend",
          document_.node(*webhooks).source);
    }
    if (module_->endpoints().empty()) {
      diagnostics_.error("E1132",
                         "OpenAPI document contains no GET/POST endpoints",
                         document_.node(root).source);
    }
    if (diagnostics_.has_errors()) {
      return std::unexpected{false};
    }
    return std::move(module_);
  }

private:
  bool expect_kind(NodeId node, NodeKind expected, std::string code,
                   std::string message) {
    if (document_.node(node).kind == expected) {
      return true;
    }
    diagnostics_.error(std::move(code), std::move(message),
                       document_.node(node).source);
    return false;
  }

  std::optional<std::string_view> scalar(NodeId node, std::string code,
                                         std::string message) {
    const auto kind = document_.node(node).kind;
    if (kind == NodeKind::string || kind == NodeKind::number) {
      return document_.scalar(node);
    }
    diagnostics_.error(std::move(code), std::move(message),
                       document_.node(node).source);
    return std::nullopt;
  }

  std::optional<bool> boolean(NodeId node, std::string code,
                              std::string message) {
    if (document_.node(node).kind == NodeKind::boolean) {
      return document_.node(node).boolean;
    }
    diagnostics_.error(std::move(code), std::move(message),
                       document_.node(node).source);
    return std::nullopt;
  }

  std::vector<NamedNode> sorted_members(NodeId object) const {
    std::vector<NamedNode> result;
    result.reserve(document_.members(object).size());
    for (const auto &member : document_.members(object)) {
      result.push_back(NamedNode{.name = document_.string(member.key),
                                 .node = member.value,
                                 .source = member.key_source});
    }
    std::ranges::sort(result, {}, &NamedNode::name);
    return result;
  }

  bool parse_version(NodeId root) {
    const auto version_node = document_.find(root, "openapi");
    if (!version_node) {
      diagnostics_.error("E1101",
                         "missing required top-level \"openapi\" field",
                         document_.node(root).source);
      return false;
    }
    const auto version =
        scalar(*version_node, "E1102", "\"openapi\" must be a string");
    if (!version) {
      return false;
    }
    if (!version->starts_with("3.1.")) {
      diagnostics_.error(
          "E1103",
          "unsupported OpenAPI version \"" + std::string{*version} + "\"",
          document_.node(*version_node).source,
          "this frontend currently accepts OpenAPI 3.1.x; 3.2 has a reserved "
          "frontend boundary but is not silently treated as 3.1");
      return false;
    }
    return true;
  }

  bool parse_metadata(NodeId root) {
    std::string_view title = "api";
    if (const auto info = document_.find(root, "info")) {
      if (!expect_kind(*info, NodeKind::object, "E1104",
                       "\"info\" must be an object")) {
        return false;
      }
      if (const auto title_node = document_.find(*info, "title")) {
        const auto parsed =
            scalar(*title_node, "E1105", "\"info.title\" must be a string");
        if (!parsed) {
          return false;
        }
        title = *parsed;
      }
    }
    module_->name = module_->intern(title);

    std::string_view base_url;
    if (const auto servers = document_.find(root, "servers")) {
      if (!expect_kind(*servers, NodeKind::array, "E1106",
                       "\"servers\" must be an array")) {
        return false;
      }
      if (!document_.elements(*servers).empty()) {
        const NodeId first = document_.elements(*servers).front();
        if (!expect_kind(first, NodeKind::object, "E1107",
                         "server entry must be an object")) {
          return false;
        }
        if (const auto url = document_.find(first, "url")) {
          const auto parsed =
              scalar(*url, "E1108", "server URL must be a string");
          if (!parsed) {
            return false;
          }
          base_url = *parsed;
        }
      }
    }
    module_->base_url = module_->intern(base_url);
    return true;
  }

  std::optional<NodeId> components(NodeId root) {
    const auto value = document_.find(root, "components");
    if (!value) {
      return std::nullopt;
    }
    if (!expect_kind(*value, NodeKind::object, "E1109",
                     "\"components\" must be an object")) {
      return std::nullopt;
    }
    return value;
  }

  bool parse_components(NodeId root) {
    const auto components_node = components(root);
    if (!components_node) {
      return !diagnostics_.has_errors();
    }
    const auto schemas = document_.find(*components_node, "schemas");
    if (!schemas) {
      return true;
    }
    if (!expect_kind(*schemas, NodeKind::object, "E1110",
                     "\"components.schemas\" must be an object")) {
      return false;
    }
    const auto entries = sorted_members(*schemas);
    for (const auto &entry : entries) {
      if (!expect_object_schema(entry.node, entry.name)) {
        return false;
      }
      if (schema_types_.contains(entry.name)) {
        diagnostics_.error(
            "E1111", "duplicate schema \"" + std::string{entry.name} + "\"",
            entry.source);
        return false;
      }
      const TypeId id =
          module_->add_struct(entry.name, document_.node(entry.node).source);
      schema_types_.emplace(std::string{entry.name}, id);
    }
    for (const auto &entry : entries) {
      if (!populate_struct(schema_types_.at(std::string{entry.name}),
                           entry.node)) {
        return false;
      }
    }
    return true;
  }

  bool expect_object_schema(NodeId schema, std::string_view name) {
    if (!expect_kind(schema, NodeKind::object, "E1112",
                     "schema \"" + std::string{name} +
                         "\" must be an object")) {
      return false;
    }
    const auto type_node = document_.find(schema, "type");
    if (!type_node) {
      diagnostics_.error(
          "E1113", "schema \"" + std::string{name} + "\" has no supported type",
          document_.node(schema).source,
          "Milestone 1 object schemas must declare type: object");
      return false;
    }
    const auto type =
        scalar(*type_node, "E1114", "schema type must be a string");
    if (!type || *type != "object") {
      diagnostics_.error(
          "E1115", "schema \"" + std::string{name} + "\" is not an object",
          document_.node(*type_node).source,
          "Milestone 1 component schemas support simple objects only");
      return false;
    }
    return true;
  }

  std::optional<TypeId> primitive_schema_type(NodeId schema,
                                              std::string_view context) {
    if (!expect_kind(schema, NodeKind::object, "E1116",
                     std::string{context} + " schema must be an object")) {
      return std::nullopt;
    }
    if (document_.find(schema, "$ref")) {
      diagnostics_.error(
          "E1117", "nested object references are not in the Milestone 1 subset",
          document_.node(schema).source,
          "use primitive string, integer, or boolean fields in this milestone");
      return std::nullopt;
    }
    const auto type_node = document_.find(schema, "type");
    if (!type_node) {
      diagnostics_.error("E1118", std::string{context} + " has no type",
                         document_.node(schema).source);
      return std::nullopt;
    }
    const auto type =
        scalar(*type_node, "E1114", "schema type must be a string");
    if (!type) {
      return std::nullopt;
    }
    if (*type == "string") {
      return string_type_;
    }
    if (*type == "integer") {
      return integer_type_;
    }
    if (*type == "boolean") {
      return boolean_type_;
    }
    diagnostics_.error(
        "E1119",
        "unsupported " + std::string{context} + " type \"" +
            std::string{*type} + "\"",
        document_.node(*type_node).source,
        "Milestone 1 supports string, integer, and boolean fields");
    return std::nullopt;
  }

  bool populate_struct(TypeId type_id, NodeId schema) {
    auto &structure =
        std::get<nair::StructType>(module_->type_mut(type_id).body);
    std::set<std::string, std::less<>> required;
    if (const auto required_node = document_.find(schema, "required")) {
      if (!expect_kind(*required_node, NodeKind::array, "E1120",
                       "\"required\" must be an array")) {
        return false;
      }
      for (const NodeId entry : document_.elements(*required_node)) {
        const auto name =
            scalar(entry, "E1121", "required field name must be a string");
        if (!name) {
          return false;
        }
        required.emplace(*name);
      }
    }
    const auto properties = document_.find(schema, "properties");
    if (!properties) {
      if (!required.empty()) {
        diagnostics_.error("E1122",
                           "required fields declared without properties",
                           document_.node(schema).source);
        return false;
      }
      return true;
    }
    if (!expect_kind(*properties, NodeKind::object, "E1123",
                     "\"properties\" must be an object")) {
      return false;
    }
    for (const auto &property : sorted_members(*properties)) {
      const auto type = primitive_schema_type(
          property.node, "property \"" + std::string{property.name} + "\"");
      if (!type) {
        return false;
      }
      structure.fields.push_back(nair::Field{
          .wire_name = module_->intern(property.name),
          .type = *type,
          .required = required.erase(std::string{property.name}) != 0U,
          .source = document_.node(property.node).source,
      });
    }
    if (!required.empty()) {
      diagnostics_.error("E1124",
                         "required field \"" + *required.begin() +
                             "\" is not declared in properties",
                         document_.node(schema).source);
      return false;
    }
    return true;
  }

  bool parse_security_schemes(NodeId root) {
    const auto components_node = components(root);
    if (diagnostics_.has_errors() || !components_node) {
      return !diagnostics_.has_errors();
    }
    const auto schemes = document_.find(*components_node, "securitySchemes");
    if (!schemes) {
      return true;
    }
    if (!expect_kind(*schemes, NodeKind::object, "E1125",
                     "\"securitySchemes\" must be an object")) {
      return false;
    }
    for (const auto &entry : sorted_members(*schemes)) {
      if (!expect_kind(entry.node, NodeKind::object, "E1126",
                       "security scheme must be an object")) {
        return false;
      }
      const auto type_node = document_.find(entry.node, "type");
      if (!type_node) {
        continue;
      }
      const auto type =
          scalar(*type_node, "E1127", "security scheme type must be a string");
      if (!type) {
        return false;
      }
      if (*type != "apiKey") {
        continue;
      }
      const auto in_node = document_.find(entry.node, "in");
      const auto name_node = document_.find(entry.node, "name");
      if (!in_node || !name_node) {
        diagnostics_.error(
            "E1128", "API-key security scheme requires \"in\" and \"name\"",
            document_.node(entry.node).source);
        return false;
      }
      const auto location =
          scalar(*in_node, "E1129", "API-key location must be a string");
      const auto wire_name =
          scalar(*name_node, "E1129", "API-key name must be a string");
      if (!location || !wire_name) {
        return false;
      }
      if (*location != "header") {
        diagnostics_.error("E1130",
                           "only header API keys are supported in Milestone 1",
                           document_.node(*in_node).source);
        return false;
      }
      auth_schemes_.emplace(std::string{entry.name},
                            AuthScheme{.wire_name = std::string{*wire_name},
                                       .source = entry.source});
    }
    root_security_ = security_requirement(root);
    return !diagnostics_.has_errors();
  }

  std::optional<std::string> security_requirement(NodeId owner) {
    const auto security = document_.find(owner, "security");
    if (!security) {
      return std::nullopt;
    }
    if (!expect_kind(*security, NodeKind::array, "E1133",
                     "\"security\" must be an array")) {
      return std::nullopt;
    }
    const auto entries = document_.elements(*security);
    if (entries.empty()) {
      return std::string{};
    }
    if (!expect_kind(entries.front(), NodeKind::object, "E1134",
                     "security requirement must be an object")) {
      return std::nullopt;
    }
    const auto requirements = sorted_members(entries.front());
    if (requirements.empty()) {
      return std::string{};
    }
    if (requirements.size() != 1U) {
      diagnostics_.error(
          "E1135", "combined security requirements are not in Milestone 1",
          document_.node(entries.front()).source);
      return std::nullopt;
    }
    return std::string{requirements.front().name};
  }

  bool activate_auth(std::string_view name, const SourceRange &source) {
    const auto found = auth_schemes_.find(name);
    if (found == auth_schemes_.end()) {
      diagnostics_.error("E1136",
                         "security requirement \"" + std::string{name} +
                             "\" is not a supported header API-key scheme",
                         source);
      return false;
    }
    if (active_auth_ && *active_auth_ != name) {
      diagnostics_.error(
          "E1137", "multiple API-key schemes are not in the Milestone 1 subset",
          source);
      return false;
    }
    active_auth_ = std::string{name};
    module_->api_key_auth = nair::ApiKeyAuth{
        .scheme_name = module_->intern(name),
        .wire_name = module_->intern(found->second.wire_name),
    };
    return true;
  }

  bool parse_paths(NodeId root) {
    const auto paths = document_.find(root, "paths");
    if (!paths) {
      diagnostics_.error("E1138", "missing required top-level \"paths\" field",
                         document_.node(root).source);
      return false;
    }
    if (!expect_kind(*paths, NodeKind::object, "E1139",
                     "\"paths\" must be an object")) {
      return false;
    }
    for (const auto &path : sorted_members(*paths)) {
      if (!path.name.starts_with('/')) {
        diagnostics_.error("E1140", "path key must start with '/'",
                           path.source);
        return false;
      }
      if (!expect_kind(path.node, NodeKind::object, "E1141",
                       "path item must be an object")) {
        return false;
      }
      for (const std::string_view method : {"delete", "patch", "put", "head"}) {
        if (const auto unsupported = document_.find(path.node, method)) {
          diagnostics_.error("E1142",
                             "HTTP method " + std::string{method} +
                                 " is outside the Milestone 1 subset",
                             document_.node(*unsupported).source);
          return false;
        }
      }
      for (const auto [name, method] :
           {std::pair{"get", nair::HttpMethod::get},
            std::pair{"post", nair::HttpMethod::post}}) {
        if (const auto operation = document_.find(path.node, name)) {
          if (!parse_operation(path, *operation, method)) {
            return false;
          }
        }
      }
    }
    return true;
  }

  bool parse_operation(const NamedNode &path, NodeId operation_node,
                       nair::HttpMethod method) {
    if (!expect_kind(operation_node, NodeKind::object, "E1143",
                     "operation must be an object")) {
      return false;
    }
    std::string operation_name =
        std::string{nair::method_name(method)} + '_' + std::string{path.name};
    if (const auto operation_id =
            document_.find(operation_node, "operationId")) {
      const auto parsed =
          scalar(*operation_id, "E1144", "\"operationId\" must be a string");
      if (!parsed) {
        return false;
      }
      operation_name = *parsed;
    }

    nair::Endpoint endpoint{module_->resource()};
    endpoint.operation_name = module_->intern(operation_name);
    endpoint.method = method;
    endpoint.path = module_->intern(path.name);
    endpoint.source = document_.node(operation_node).source;

    std::map<std::pair<nair::ParameterLocation, std::string>, nair::Parameter>
        parameters;
    if (const auto path_parameters = document_.find(path.node, "parameters");
        path_parameters &&
        !parse_parameters(*path_parameters, parameters, false)) {
      return false;
    }
    if (const auto operation_parameters =
            document_.find(operation_node, "parameters");
        operation_parameters &&
        !parse_parameters(*operation_parameters, parameters, true)) {
      return false;
    }
    for (auto &[key, parameter] : parameters) {
      (void)key;
      endpoint.parameters.push_back(std::move(parameter));
    }
    if (!validate_path_parameters(path.name, endpoint.parameters,
                                  endpoint.source)) {
      return false;
    }

    if (const auto body = document_.find(operation_node, "requestBody")) {
      const auto parsed = parse_request_body(*body, operation_name);
      if (!parsed) {
        return false;
      }
      endpoint.request_body = *parsed;
    }
    const auto response = parse_response(operation_node, operation_name);
    if (!response) {
      return false;
    }
    endpoint.response = *response;

    std::optional<std::string> security = root_security_;
    if (document_.find(operation_node, "security")) {
      security = security_requirement(operation_node);
      if (diagnostics_.has_errors()) {
        return false;
      }
    }
    if (security && !security->empty()) {
      if (!activate_auth(*security, endpoint.source)) {
        return false;
      }
      endpoint.authenticated = true;
    }
    (void)module_->add_endpoint(std::move(endpoint));
    return true;
  }

  bool parse_parameters(
      NodeId parameter_array,
      std::map<std::pair<nair::ParameterLocation, std::string>, nair::Parameter>
          &parameters,
      bool replace) {
    if (!expect_kind(parameter_array, NodeKind::array, "E1145",
                     "\"parameters\" must be an array")) {
      return false;
    }
    for (const NodeId parameter_node : document_.elements(parameter_array)) {
      if (!expect_kind(parameter_node, NodeKind::object, "E1146",
                       "parameter must be an object")) {
        return false;
      }
      if (document_.find(parameter_node, "$ref")) {
        diagnostics_.error("E1147",
                           "parameter $ref is not in the Milestone 1 subset",
                           document_.node(parameter_node).source);
        return false;
      }
      const auto name_node = document_.find(parameter_node, "name");
      const auto in_node = document_.find(parameter_node, "in");
      const auto schema_node = document_.find(parameter_node, "schema");
      if (!name_node || !in_node || !schema_node) {
        diagnostics_.error(
            "E1148", "parameter requires \"name\", \"in\", and \"schema\"",
            document_.node(parameter_node).source);
        return false;
      }
      const auto name =
          scalar(*name_node, "E1149", "parameter name must be a string");
      const auto location =
          scalar(*in_node, "E1149", "parameter location must be a string");
      const auto type = primitive_schema_type(*schema_node, "parameter");
      if (!name || !location || !type) {
        return false;
      }
      nair::ParameterLocation parsed_location{};
      if (*location == "path") {
        parsed_location = nair::ParameterLocation::path;
      } else if (*location == "query") {
        parsed_location = nair::ParameterLocation::query;
      } else {
        diagnostics_.error("E1150",
                           "parameter location \"" + std::string{*location} +
                               "\" is outside the Milestone 1 subset",
                           document_.node(*in_node).source);
        return false;
      }
      bool required = parsed_location == nair::ParameterLocation::path;
      if (const auto required_node =
              document_.find(parameter_node, "required")) {
        const auto parsed = boolean(*required_node, "E1151",
                                    "parameter required must be boolean");
        if (!parsed) {
          return false;
        }
        required = *parsed;
      }
      if (parsed_location == nair::ParameterLocation::path && !required) {
        diagnostics_.error("E1152", "path parameters must be required",
                           document_.node(parameter_node).source);
        return false;
      }
      const auto key = std::pair{parsed_location, std::string{*name}};
      nair::Parameter parameter{
          .wire_name = module_->intern(*name),
          .type = *type,
          .location = parsed_location,
          .required = required,
          .source = document_.node(parameter_node).source,
      };
      const auto found = parameters.find(key);
      if (found != parameters.end() && !replace) {
        diagnostics_.error("E1153",
                           "duplicate parameter \"" + std::string{*name} + "\"",
                           document_.node(parameter_node).source);
        return false;
      }
      parameters.insert_or_assign(key, std::move(parameter));
    }
    return true;
  }

  bool validate_path_parameters(std::string_view path,
                                std::span<const nair::Parameter> parameters,
                                const SourceRange &source) {
    std::set<std::string, std::less<>> declared;
    for (const auto &parameter : parameters) {
      if (parameter.location == nair::ParameterLocation::path) {
        declared.emplace(module_->string(parameter.wire_name));
      }
    }
    std::set<std::string, std::less<>> used;
    std::size_t cursor = 0U;
    while (cursor < path.size()) {
      const auto open = path.find('{', cursor);
      if (open == std::string_view::npos) {
        break;
      }
      const auto close = path.find('}', open + 1U);
      if (close == std::string_view::npos || close == open + 1U) {
        diagnostics_.error("E1154", "malformed path parameter template",
                           source);
        return false;
      }
      used.emplace(path.substr(open + 1U, close - open - 1U));
      cursor = close + 1U;
    }
    if (used != declared) {
      diagnostics_.error(
          "E1155", "path template and declared path parameters do not match",
          source);
      return false;
    }
    return true;
  }

  std::optional<TypeId> resolve_body_schema(NodeId schema,
                                            std::string_view suggested_name) {
    if (!expect_kind(schema, NodeKind::object, "E1156",
                     "body schema must be an object")) {
      return std::nullopt;
    }
    if (const auto ref_node = document_.find(schema, "$ref")) {
      const auto reference =
          scalar(*ref_node, "E1157", "schema $ref must be a string");
      if (!reference) {
        return std::nullopt;
      }
      constexpr std::string_view prefix = "#/components/schemas/";
      if (!reference->starts_with(prefix)) {
        diagnostics_.error(
            "E1158", "only local component schema references are supported",
            document_.node(*ref_node).source);
        return std::nullopt;
      }
      const auto name = reference->substr(prefix.size());
      const auto found = schema_types_.find(name);
      if (found == schema_types_.end()) {
        diagnostics_.error("E1159",
                           "unresolved schema reference \"" +
                               std::string{*reference} + "\"",
                           document_.node(*ref_node).source);
        return std::nullopt;
      }
      return found->second;
    }
    if (!expect_object_schema(schema, suggested_name)) {
      return std::nullopt;
    }
    const TypeId id =
        module_->add_struct(suggested_name, document_.node(schema).source);
    if (!populate_struct(id, schema)) {
      return std::nullopt;
    }
    return id;
  }

  std::optional<NodeId> json_content_schema(NodeId content_owner,
                                            std::string_view context) {
    const auto content = document_.find(content_owner, "content");
    if (!content ||
        !expect_kind(*content, NodeKind::object, "E1160",
                     std::string{context} + " content must be an object")) {
      if (!content) {
        diagnostics_.error("E1161",
                           std::string{context} +
                               " requires application/json content",
                           document_.node(content_owner).source);
      }
      return std::nullopt;
    }
    const auto json = document_.find(*content, "application/json");
    if (!json ||
        !expect_kind(*json, NodeKind::object, "E1162",
                     "application/json media type must be an object")) {
      if (!json) {
        diagnostics_.error("E1163",
                           std::string{context} +
                               " has no application/json media type",
                           document_.node(*content).source);
      }
      return std::nullopt;
    }
    const auto schema = document_.find(*json, "schema");
    if (!schema) {
      diagnostics_.error("E1164", std::string{context} + " has no schema",
                         document_.node(*json).source);
    }
    return schema;
  }

  std::optional<nair::RequestBody>
  parse_request_body(NodeId body, std::string_view operation_name) {
    if (!expect_kind(body, NodeKind::object, "E1165",
                     "requestBody must be an object")) {
      return std::nullopt;
    }
    bool required = false;
    if (const auto required_node = document_.find(body, "required")) {
      const auto parsed = boolean(*required_node, "E1166",
                                  "requestBody required must be boolean");
      if (!parsed) {
        return std::nullopt;
      }
      required = *parsed;
    }
    const auto schema = json_content_schema(body, "request body");
    if (!schema) {
      return std::nullopt;
    }
    const auto type =
        resolve_body_schema(*schema, std::string{operation_name} + "_request");
    if (!type) {
      return std::nullopt;
    }
    return nair::RequestBody{.type = *type, .required = required};
  }

  std::optional<nair::Response>
  parse_response(NodeId operation, std::string_view operation_name) {
    const auto responses = document_.find(operation, "responses");
    if (!responses || !expect_kind(*responses, NodeKind::object, "E1167",
                                   "\"responses\" must be an object")) {
      if (!responses) {
        diagnostics_.error("E1168", "operation has no responses",
                           document_.node(operation).source);
      }
      return std::nullopt;
    }
    std::vector<std::pair<int, NodeId>> successful;
    for (const auto &entry : sorted_members(*responses)) {
      int code = 0;
      const auto parsed = std::from_chars(
          entry.name.data(), entry.name.data() + entry.name.size(), code);
      if (parsed.ec == std::errc{} &&
          parsed.ptr == entry.name.data() + entry.name.size() && code >= 200 &&
          code <= 299) {
        successful.emplace_back(code, entry.node);
      }
    }
    std::ranges::sort(successful, {}, &std::pair<int, NodeId>::first);
    if (successful.empty()) {
      diagnostics_.error("E1169", "operation has no typed 2xx response",
                         document_.node(*responses).source);
      return std::nullopt;
    }
    const auto [http_status, response_node] = successful.front();
    if (!expect_kind(response_node, NodeKind::object, "E1170",
                     "response must be an object")) {
      return std::nullopt;
    }
    const auto schema = json_content_schema(response_node, "response");
    if (!schema) {
      return std::nullopt;
    }
    const auto type =
        resolve_body_schema(*schema, std::string{operation_name} + "_response");
    if (!type) {
      return std::nullopt;
    }
    return nair::Response{.http_status = http_status, .type = *type};
  }

  const ParsedDocument &document_;
  DiagnosticBag &diagnostics_;
  std::unique_ptr<nair::Module> module_;
  TypeId string_type_;
  TypeId integer_type_;
  TypeId boolean_type_;
  std::map<std::string, TypeId, std::less<>> schema_types_;
  std::map<std::string, AuthScheme, std::less<>> auth_schemes_;
  std::optional<std::string> root_security_;
  std::optional<std::string> active_auth_;
};

} // namespace

std::expected<std::unique_ptr<nair::Module>, bool>
lower_to_nair(const ParsedDocument &document, DiagnosticBag &diagnostics) {
  try {
    return Lowerer{document, diagnostics}.run();
  } catch (const std::bad_alloc &) {
    diagnostics.error("E1198", "memory allocation failed while lowering NAIR");
  } catch (const std::exception &exception) {
    diagnostics.error("E1199", std::string{"internal NAIR lowering failure: "} +
                                   exception.what());
  }
  return std::unexpected{false};
}

} // namespace sdkcc::compiler::openapi
