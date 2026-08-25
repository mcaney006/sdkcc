#include <sdkcc/compiler/source/string_interner.hpp>

#include <cstring>
#include <limits>
#include <stdexcept>

namespace sdkcc::compiler {

StringInterner::StringInterner(std::pmr::memory_resource *resource)
    : resource_{resource}, index_{resource}, values_{resource} {
  if (resource_ == nullptr) {
    throw std::invalid_argument{"StringInterner requires a memory resource"};
  }
}

std::size_t
StringInterner::Hash::operator()(std::string_view value) const noexcept {
  // FNV-1a is stable and sufficient for an internal hash table; table order
  // never affects emitted output.
  std::size_t hash = sizeof(std::size_t) == 8U
                         ? std::size_t{1469598103934665603ULL}
                         : std::size_t{2166136261U};
  const std::size_t prime = sizeof(std::size_t) == 8U
                                ? std::size_t{1099511628211ULL}
                                : std::size_t{16777619U};
  for (const char source_byte : value) {
    const auto byte = static_cast<unsigned char>(source_byte);
    hash ^= static_cast<std::size_t>(byte);
    hash *= prime;
  }
  return hash;
}

StringId StringInterner::intern(std::string_view value) {
  if (const auto found = index_.find(value); found != index_.end()) {
    return found->second;
  }
  if (values_.size() >=
      static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
    throw std::length_error{"string interner ID space exhausted"};
  }
  auto *const memory = static_cast<char *>(
      resource_->allocate(value.size() + 1U, alignof(char)));
  if (!value.empty()) {
    std::memcpy(memory, value.data(), value.size());
  }
  memory[value.size()] = '\0';
  const std::string_view stored{memory, value.size()};
  const auto id = StringId{static_cast<std::uint32_t>(values_.size())};
  values_.push_back(stored);
  index_.emplace(stored, id);
  return id;
}

std::string_view StringInterner::get(StringId id) const {
  if (!id.valid() || static_cast<std::size_t>(id.value()) >= values_.size()) {
    throw std::out_of_range{"invalid StringId"};
  }
  return values_[id.value()];
}

} // namespace sdkcc::compiler
