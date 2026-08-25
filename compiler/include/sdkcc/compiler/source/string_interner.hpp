#ifndef SDKCC_COMPILER_SOURCE_STRING_INTERNER_HPP
#define SDKCC_COMPILER_SOURCE_STRING_INTERNER_HPP

#include <sdkcc/compiler/ids.hpp>

#include <memory_resource>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace sdkcc::compiler {

class StringInterner final {
public:
  explicit StringInterner(std::pmr::memory_resource *resource);

  [[nodiscard]] StringId intern(std::string_view value);
  [[nodiscard]] std::string_view get(StringId id) const;
  [[nodiscard]] std::size_t size() const noexcept {
    return values_.size();
  }

private:
  struct Hash {
    using is_transparent = void;
    [[nodiscard]] std::size_t operator()(std::string_view value) const noexcept;
  };

  std::pmr::memory_resource *resource_;
  std::pmr::unordered_map<std::string_view, StringId, Hash, std::equal_to<>>
      index_;
  std::pmr::vector<std::string_view> values_;
};

} // namespace sdkcc::compiler

#endif
