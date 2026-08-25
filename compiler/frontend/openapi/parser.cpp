#include <sdkcc/compiler/frontend/openapi/parser.hpp>

#include <simdjson.h>
#include <yaml.h>

#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace sdkcc::compiler::openapi {
namespace {

SourceRange source_range(std::string_view file, std::size_t begin_line,
                         std::size_t begin_column, std::size_t begin_offset,
                         std::size_t end_line, std::size_t end_column,
                         std::size_t end_offset) {
  return SourceRange{
      .begin = {.file = std::string{file},
                .line = begin_line + 1U,
                .column = begin_column + 1U,
                .offset = begin_offset},
      .end = {.file = std::string{file},
              .line = end_line + 1U,
              .column = end_column + 1U,
              .offset = end_offset},
  };
}

SourceRange yaml_range(std::string_view file, const yaml_mark_t &begin,
                       const yaml_mark_t &end) {
  return source_range(file, begin.line, begin.column, begin.index, end.line,
                      end.column, end.index);
}

SourceRange json_range(std::string_view file) {
  return source_range(file, 0U, 0U, 0U, 0U, 0U, 0U);
}

bool looks_numeric(std::string_view value) {
  if (value.empty()) {
    return false;
  }
  std::size_t index = value.front() == '-' ? 1U : 0U;
  if (index == value.size()) {
    return false;
  }
  bool digit = false;
  for (; index < value.size(); ++index) {
    const char byte = value[index];
    if (byte >= '0' && byte <= '9') {
      digit = true;
      continue;
    }
    if (byte != '.' && byte != 'e' && byte != 'E' && byte != '+' &&
        byte != '-') {
      return false;
    }
  }
  return digit;
}

NodeKind yaml_scalar_kind(std::string_view value) {
  if (value == "null" || value == "~") {
    return NodeKind::null_value;
  }
  if (value == "true" || value == "false") {
    return NodeKind::boolean;
  }
  return looks_numeric(value) ? NodeKind::number : NodeKind::string;
}

class YamlBuilder final {
public:
  YamlBuilder(yaml_document_t &yaml, ParsedDocument &target,
              DiagnosticBag &diagnostics, ParseLimits limits)
      : yaml_{yaml}, target_{target}, diagnostics_{diagnostics},
        limits_{limits} {
    const auto count =
        static_cast<std::size_t>(yaml_.nodes.top - yaml_.nodes.start);
    states_.resize(count + 1U);
    translated_.resize(count + 1U);
  }

  std::optional<NodeId> build(int yaml_id, std::size_t depth) {
    if (depth > limits_.max_depth) {
      diagnostics_.error("E1003", "YAML nesting limit exceeded");
      return std::nullopt;
    }
    if (yaml_id <= 0 || static_cast<std::size_t>(yaml_id) >= states_.size()) {
      diagnostics_.error("E1004", "invalid YAML node reference");
      return std::nullopt;
    }
    if (states_[static_cast<std::size_t>(yaml_id)] == 1U) {
      const yaml_node_t *const node = yaml_document_get_node(&yaml_, yaml_id);
      diagnostics_.error(
          "E1005", "cyclic YAML alias is not allowed",
          node == nullptr
              ? std::nullopt
              : std::optional{yaml_range(target_.source_name(),
                                         node->start_mark, node->end_mark)});
      return std::nullopt;
    }
    if (states_[static_cast<std::size_t>(yaml_id)] == 2U) {
      return translated_[static_cast<std::size_t>(yaml_id)];
    }
    if (target_.node_count() >= limits_.max_nodes) {
      diagnostics_.error("E1006", "parsed node limit exceeded");
      return std::nullopt;
    }
    yaml_node_t *const node = yaml_document_get_node(&yaml_, yaml_id);
    if (node == nullptr) {
      diagnostics_.error("E1004", "invalid YAML node reference");
      return std::nullopt;
    }
    states_[static_cast<std::size_t>(yaml_id)] = 1U;
    const auto range =
        yaml_range(target_.source_name(), node->start_mark, node->end_mark);

    std::optional<NodeId> result;
    switch (node->type) {
    case YAML_SCALAR_NODE:
      result = build_scalar(*node, range);
      break;
    case YAML_SEQUENCE_NODE:
      result = build_sequence(*node, range, depth);
      break;
    case YAML_MAPPING_NODE:
      result = build_mapping(*node, range, depth);
      break;
    case YAML_NO_NODE:
      diagnostics_.error("E1007", "invalid empty YAML node", range);
      break;
    }
    if (!result) {
      return std::nullopt;
    }
    translated_[static_cast<std::size_t>(yaml_id)] = *result;
    states_[static_cast<std::size_t>(yaml_id)] = 2U;
    return result;
  }

private:
  std::optional<NodeId> build_scalar(const yaml_node_t &node,
                                     const SourceRange &range) {
    const std::string_view value{
        reinterpret_cast<const char *>(node.data.scalar.value),
        node.data.scalar.length};
    const NodeKind kind = yaml_scalar_kind(value);
    const NodeId id = target_.add_node(kind, range);
    if (kind == NodeKind::boolean) {
      target_.set_boolean(id, value == "true");
    } else if (kind == NodeKind::string || kind == NodeKind::number) {
      target_.set_scalar(id, value);
    }
    return id;
  }

  std::optional<NodeId> build_sequence(const yaml_node_t &node,
                                       const SourceRange &range,
                                       std::size_t depth) {
    const NodeId id = target_.add_node(NodeKind::array, range);
    for (const yaml_node_item_t *item = node.data.sequence.items.start;
         item != node.data.sequence.items.top; ++item) {
      const auto child = build(*item, depth + 1U);
      if (!child) {
        return std::nullopt;
      }
      target_.add_element(id, *child);
    }
    return id;
  }

  std::optional<NodeId> build_mapping(const yaml_node_t &node,
                                      const SourceRange &range,
                                      std::size_t depth) {
    const NodeId id = target_.add_node(NodeKind::object, range);
    std::set<std::string, std::less<>> keys;
    for (const yaml_node_pair_t *pair = node.data.mapping.pairs.start;
         pair != node.data.mapping.pairs.top; ++pair) {
      yaml_node_t *const key_node = yaml_document_get_node(&yaml_, pair->key);
      if (key_node == nullptr || key_node->type != YAML_SCALAR_NODE) {
        diagnostics_.error("E1008", "YAML mapping keys must be scalars", range);
        return std::nullopt;
      }
      const std::string_view key{
          reinterpret_cast<const char *>(key_node->data.scalar.value),
          key_node->data.scalar.length};
      const auto key_source = yaml_range(
          target_.source_name(), key_node->start_mark, key_node->end_mark);
      if (key.size() > limits_.max_key_bytes) {
        diagnostics_.error("E1009", "YAML mapping key exceeds byte limit",
                           key_source);
        return std::nullopt;
      }
      if (!keys.emplace(key).second) {
        diagnostics_.error(
            "E1010", "duplicate YAML mapping key \"" + std::string{key} + "\"",
            key_source);
        return std::nullopt;
      }
      const auto child = build(pair->value, depth + 1U);
      if (!child) {
        return std::nullopt;
      }
      target_.add_member(id, key, *child, key_source);
    }
    return id;
  }

  yaml_document_t &yaml_;
  ParsedDocument &target_;
  DiagnosticBag &diagnostics_;
  ParseLimits limits_;
  std::vector<std::uint8_t> states_;
  std::vector<NodeId> translated_;
};

std::expected<std::unique_ptr<ParsedDocument>, bool>
parse_yaml(std::string_view source_name, std::span<const std::byte> input,
           DiagnosticBag &diagnostics, ParseLimits limits) {
  yaml_parser_t parser{};
  if (yaml_parser_initialize(&parser) == 0) {
    diagnostics.error("E1001", "libyaml parser initialization failed");
    return std::unexpected{false};
  }
  struct ParserGuard {
    yaml_parser_t *parser;
    ~ParserGuard() {
      yaml_parser_delete(parser);
    }
  } parser_guard{&parser};
  yaml_parser_set_input_string(
      &parser, reinterpret_cast<const unsigned char *>(input.data()),
      input.size());

  yaml_document_t yaml{};
  if (yaml_parser_load(&parser, &yaml) == 0) {
    const std::string problem =
        parser.problem == nullptr ? "invalid YAML" : parser.problem;
    diagnostics.error(
        "E1002", problem,
        source_range(source_name, parser.problem_mark.line,
                     parser.problem_mark.column, parser.problem_mark.index,
                     parser.problem_mark.line, parser.problem_mark.column + 1U,
                     parser.problem_mark.index + 1U));
    return std::unexpected{false};
  }
  struct DocumentGuard {
    yaml_document_t *document;
    ~DocumentGuard() {
      yaml_document_delete(document);
    }
  } document_guard{&yaml};

  yaml_node_t *const root = yaml_document_get_root_node(&yaml);
  if (root == nullptr) {
    diagnostics.error("E1011", "API description is empty");
    return std::unexpected{false};
  }
  const int root_id =
      static_cast<int>((root - yaml.nodes.start) + std::ptrdiff_t{1});
  auto document = std::make_unique<ParsedDocument>(std::string{source_name});
  YamlBuilder builder{yaml, *document, diagnostics, limits};
  const auto translated_root = builder.build(root_id, 0U);
  if (!translated_root) {
    return std::unexpected{false};
  }
  document->set_root(*translated_root);

  yaml_document_t trailing{};
  if (yaml_parser_load(&parser, &trailing) == 0) {
    const std::string problem =
        parser.problem == nullptr ? "invalid trailing YAML" : parser.problem;
    diagnostics.error(
        "E1012", problem,
        source_range(source_name, parser.problem_mark.line,
                     parser.problem_mark.column, parser.problem_mark.index,
                     parser.problem_mark.line, parser.problem_mark.column + 1U,
                     parser.problem_mark.index + 1U));
    return std::unexpected{false};
  }
  const bool has_trailing_document =
      yaml_document_get_root_node(&trailing) != nullptr;
  yaml_document_delete(&trailing);
  if (has_trailing_document) {
    diagnostics.error("E1013",
                      "multiple YAML documents are not allowed in one API "
                      "description");
    return std::unexpected{false};
  }
  return document;
}

class JsonBuilder final {
public:
  JsonBuilder(ParsedDocument &target, DiagnosticBag &diagnostics,
              ParseLimits limits)
      : target_{target}, diagnostics_{diagnostics}, limits_{limits} {}

  std::optional<NodeId> build(simdjson::dom::element element,
                              std::size_t depth) {
    if (depth > limits_.max_depth) {
      diagnostics_.error("E1020", "JSON nesting limit exceeded");
      return std::nullopt;
    }
    if (target_.node_count() >= limits_.max_nodes) {
      diagnostics_.error("E1006", "parsed node limit exceeded");
      return std::nullopt;
    }
    const auto source = json_range(target_.source_name());
    switch (element.type()) {
    case simdjson::dom::element_type::NULL_VALUE:
      return target_.add_node(NodeKind::null_value, source);
    case simdjson::dom::element_type::BOOL: {
      bool value = false;
      if (element.get(value) != simdjson::SUCCESS) {
        return fail("could not read JSON boolean");
      }
      const NodeId id = target_.add_node(NodeKind::boolean, source);
      target_.set_boolean(id, value);
      return id;
    }
    case simdjson::dom::element_type::STRING: {
      std::string_view value;
      if (element.get(value) != simdjson::SUCCESS) {
        return fail("could not read JSON string");
      }
      const NodeId id = target_.add_node(NodeKind::string, source);
      target_.set_scalar(id, value);
      return id;
    }
    case simdjson::dom::element_type::INT64:
    case simdjson::dom::element_type::UINT64:
    case simdjson::dom::element_type::DOUBLE:
      return build_number(element, source);
    case simdjson::dom::element_type::BIGINT:
      return fail("JSON integer is outside the supported parser range");
    case simdjson::dom::element_type::ARRAY:
      return build_array(element, source, depth);
    case simdjson::dom::element_type::OBJECT:
      return build_object(element, source, depth);
    }
    return fail("unknown JSON value type");
  }

private:
  std::optional<NodeId> fail(std::string message) {
    diagnostics_.error("E1021", std::move(message));
    return std::nullopt;
  }

  std::optional<NodeId> build_number(simdjson::dom::element element,
                                     const SourceRange &source) {
    char text[64]{};
    char *end = nullptr;
    std::errc error{};
    if (element.type() == simdjson::dom::element_type::INT64) {
      std::int64_t value{};
      if (element.get(value) != simdjson::SUCCESS) {
        return fail("could not read JSON integer");
      }
      const auto result =
          std::to_chars(std::begin(text), std::end(text), value);
      end = result.ptr;
      error = result.ec;
    } else if (element.type() == simdjson::dom::element_type::UINT64) {
      std::uint64_t value{};
      if (element.get(value) != simdjson::SUCCESS) {
        return fail("could not read JSON unsigned integer");
      }
      const auto result =
          std::to_chars(std::begin(text), std::end(text), value);
      end = result.ptr;
      error = result.ec;
    } else {
      double value{};
      if (element.get(value) != simdjson::SUCCESS || !std::isfinite(value)) {
        return fail("could not read finite JSON number");
      }
      const auto result = std::to_chars(std::begin(text), std::end(text), value,
                                        std::chars_format::general);
      end = result.ptr;
      error = result.ec;
    }
    if (error != std::errc{}) {
      return fail("could not format JSON number");
    }
    const NodeId id = target_.add_node(NodeKind::number, source);
    target_.set_scalar(
        id, std::string_view{text, static_cast<std::size_t>(end - text)});
    return id;
  }

  std::optional<NodeId> build_array(simdjson::dom::element element,
                                    const SourceRange &source,
                                    std::size_t depth) {
    simdjson::dom::array array;
    if (element.get(array) != simdjson::SUCCESS) {
      return fail("could not read JSON array");
    }
    const NodeId id = target_.add_node(NodeKind::array, source);
    for (const auto child : array) {
      const auto translated = build(child, depth + 1U);
      if (!translated) {
        return std::nullopt;
      }
      target_.add_element(id, *translated);
    }
    return id;
  }

  std::optional<NodeId> build_object(simdjson::dom::element element,
                                     const SourceRange &source,
                                     std::size_t depth) {
    simdjson::dom::object object;
    if (element.get(object) != simdjson::SUCCESS) {
      return fail("could not read JSON object");
    }
    const NodeId id = target_.add_node(NodeKind::object, source);
    std::set<std::string, std::less<>> keys;
    for (const auto field : object) {
      const auto key = field.key;
      if (key.size() > limits_.max_key_bytes) {
        return fail("JSON object key exceeds byte limit");
      }
      if (!keys.emplace(key).second) {
        return fail("duplicate JSON object key \"" + std::string{key} + "\"");
      }
      const auto translated = build(field.value, depth + 1U);
      if (!translated) {
        return std::nullopt;
      }
      target_.add_member(id, key, *translated, source);
    }
    return id;
  }

  ParsedDocument &target_;
  DiagnosticBag &diagnostics_;
  ParseLimits limits_;
};

std::expected<std::unique_ptr<ParsedDocument>, bool>
parse_json(std::string_view source_name, std::span<const std::byte> input,
           DiagnosticBag &diagnostics, ParseLimits limits) {
  simdjson::dom::parser parser;
  simdjson::dom::element root;
  const auto parse_error =
      parser
          .parse(reinterpret_cast<const std::uint8_t *>(input.data()),
                 input.size(), true)
          .get(root);
  if (parse_error != simdjson::SUCCESS) {
    diagnostics.error("E1022",
                      std::string{"invalid JSON: "} +
                          simdjson::error_message(parse_error),
                      json_range(source_name));
    return std::unexpected{false};
  }
  auto document = std::make_unique<ParsedDocument>(std::string{source_name});
  JsonBuilder builder{*document, diagnostics, limits};
  const auto translated = builder.build(root, 0U);
  if (!translated) {
    return std::unexpected{false};
  }
  document->set_root(*translated);
  return document;
}

bool select_json(std::string_view source_name,
                 std::span<const std::byte> input) {
  const auto extension =
      std::filesystem::path{source_name}.extension().string();
  if (extension == ".json") {
    return true;
  }
  if (extension == ".yaml" || extension == ".yml") {
    return false;
  }
  for (const std::byte byte : input) {
    const char value = static_cast<char>(byte);
    if (value != ' ' && value != '\t' && value != '\r' && value != '\n') {
      return value == '{' || value == '[';
    }
  }
  return false;
}

} // namespace

std::expected<std::unique_ptr<ParsedDocument>, bool>
parse_document(std::string_view source_name, std::span<const std::byte> input,
               DiagnosticBag &diagnostics, ParseLimits limits) {
  if (input.size() > limits.max_document_bytes) {
    diagnostics.error("E1000",
                      "API description exceeds the configured byte limit",
                      std::nullopt,
                      "default limit is " +
                          std::to_string(limits.max_document_bytes) + " bytes");
    return std::unexpected{false};
  }
  if (input.empty()) {
    diagnostics.error("E1011", "API description is empty");
    return std::unexpected{false};
  }
  try {
    return select_json(source_name, input)
               ? parse_json(source_name, input, diagnostics, limits)
               : parse_yaml(source_name, input, diagnostics, limits);
  } catch (const std::bad_alloc &) {
    diagnostics.error("E1098", "memory allocation failed while parsing");
  } catch (const std::exception &exception) {
    diagnostics.error("E1099", std::string{"internal parser failure: "} +
                                   exception.what());
  }
  return std::unexpected{false};
}

} // namespace sdkcc::compiler::openapi
