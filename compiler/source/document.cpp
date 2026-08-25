#include <sdkcc/compiler/source/document.hpp>

#include <limits>
#include <stdexcept>

namespace sdkcc::compiler {

DocumentNode::DocumentNode(NodeKind node_kind, SourceRange node_source,
                           std::pmr::memory_resource *resource)
    : kind{node_kind}, source{std::move(node_source)}, elements{resource},
      members{resource} {}

ParsedDocument::ParsedDocument(std::string source_name)
    : source_name_{std::move(source_name)}, arena_{}, strings_{&arena_},
      nodes_{&arena_} {}

NodeId ParsedDocument::add_node(NodeKind kind, SourceRange source) {
  if (nodes_.size() >=
      static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
    throw std::length_error{"parsed document node ID space exhausted"};
  }
  const auto id = NodeId{static_cast<std::uint32_t>(nodes_.size())};
  nodes_.emplace_back(kind, std::move(source), &arena_);
  return id;
}

void ParsedDocument::set_scalar(NodeId id, std::string_view value) {
  auto &target = mutable_node(id);
  if (target.kind != NodeKind::string && target.kind != NodeKind::number) {
    throw std::logic_error{"scalar assigned to non-scalar node"};
  }
  target.scalar = strings_.intern(value);
}

void ParsedDocument::set_boolean(NodeId id, bool value) {
  auto &target = mutable_node(id);
  if (target.kind != NodeKind::boolean) {
    throw std::logic_error{"boolean assigned to non-boolean node"};
  }
  target.boolean = value;
}

void ParsedDocument::add_element(NodeId array, NodeId value) {
  auto &target = mutable_node(array);
  if (target.kind != NodeKind::array) {
    throw std::logic_error{"element added to non-array node"};
  }
  target.elements.push_back(value);
}

void ParsedDocument::add_member(NodeId object, std::string_view key,
                                NodeId value, SourceRange key_source) {
  auto &target = mutable_node(object);
  if (target.kind != NodeKind::object) {
    throw std::logic_error{"member added to non-object node"};
  }
  target.members.push_back(DocumentMember{
      .key = strings_.intern(key),
      .value = value,
      .key_source = std::move(key_source),
  });
}

const DocumentNode &ParsedDocument::node(NodeId id) const {
  if (!id.valid() || static_cast<std::size_t>(id.value()) >= nodes_.size()) {
    throw std::out_of_range{"invalid NodeId"};
  }
  return nodes_[id.value()];
}

DocumentNode &ParsedDocument::mutable_node(NodeId id) {
  if (!id.valid() || static_cast<std::size_t>(id.value()) >= nodes_.size()) {
    throw std::out_of_range{"invalid NodeId"};
  }
  return nodes_[id.value()];
}

std::string_view ParsedDocument::scalar(NodeId id) const {
  const auto &source = node(id);
  if ((source.kind != NodeKind::string && source.kind != NodeKind::number) ||
      !source.scalar.valid()) {
    throw std::logic_error{"node is not a scalar"};
  }
  return strings_.get(source.scalar);
}

std::optional<NodeId> ParsedDocument::find(NodeId object,
                                           std::string_view key) const {
  const auto &source = node(object);
  if (source.kind != NodeKind::object) {
    return std::nullopt;
  }
  for (const auto &member : source.members) {
    if (strings_.get(member.key) == key) {
      return member.value;
    }
  }
  return std::nullopt;
}

std::span<const DocumentMember> ParsedDocument::members(NodeId object) const {
  const auto &source = node(object);
  if (source.kind != NodeKind::object) {
    return {};
  }
  return source.members;
}

std::span<const NodeId> ParsedDocument::elements(NodeId array) const {
  const auto &source = node(array);
  if (source.kind != NodeKind::array) {
    return {};
  }
  return source.elements;
}

} // namespace sdkcc::compiler
