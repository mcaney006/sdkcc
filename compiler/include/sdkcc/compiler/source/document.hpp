#ifndef SDKCC_COMPILER_SOURCE_DOCUMENT_HPP
#define SDKCC_COMPILER_SOURCE_DOCUMENT_HPP

#include <sdkcc/compiler/ids.hpp>
#include <sdkcc/compiler/source/string_interner.hpp>
#include <sdkcc/compiler/source_location.hpp>

#include <memory_resource>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace sdkcc::compiler {

enum class NodeKind : std::uint8_t {
  null_value,
  boolean,
  number,
  string,
  array,
  object,
};

struct DocumentMember {
  StringId key;
  NodeId value;
  SourceRange key_source;
};

struct DocumentNode {
  explicit DocumentNode(NodeKind kind, SourceRange source,
                        std::pmr::memory_resource *resource);

  NodeKind kind;
  SourceRange source;
  StringId scalar;
  bool boolean{};
  std::pmr::vector<NodeId> elements;
  std::pmr::vector<DocumentMember> members;
};

class ParsedDocument final {
public:
  explicit ParsedDocument(std::string source_name);
  ParsedDocument(const ParsedDocument &) = delete;
  ParsedDocument &operator=(const ParsedDocument &) = delete;

  [[nodiscard]] NodeId add_node(NodeKind kind, SourceRange source);
  void set_scalar(NodeId id, std::string_view value);
  void set_boolean(NodeId id, bool value);
  void add_element(NodeId array, NodeId value);
  void add_member(NodeId object, std::string_view key, NodeId value,
                  SourceRange key_source);
  void set_root(NodeId root) {
    root_ = root;
  }

  [[nodiscard]] NodeId root() const noexcept {
    return root_;
  }
  [[nodiscard]] const DocumentNode &node(NodeId id) const;
  [[nodiscard]] std::string_view scalar(NodeId id) const;
  [[nodiscard]] std::string_view string(StringId id) const {
    return strings_.get(id);
  }
  [[nodiscard]] std::optional<NodeId> find(NodeId object,
                                           std::string_view key) const;
  [[nodiscard]] std::span<const DocumentMember> members(NodeId object) const;
  [[nodiscard]] std::span<const NodeId> elements(NodeId array) const;
  [[nodiscard]] std::string_view source_name() const noexcept {
    return source_name_;
  }
  [[nodiscard]] std::size_t node_count() const noexcept {
    return nodes_.size();
  }

private:
  [[nodiscard]] DocumentNode &mutable_node(NodeId id);

  std::string source_name_;
  std::pmr::monotonic_buffer_resource arena_;
  StringInterner strings_;
  std::pmr::vector<DocumentNode> nodes_;
  NodeId root_;
};

} // namespace sdkcc::compiler

#endif
