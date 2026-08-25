#ifndef SDKCC_COMPILER_IDS_HPP
#define SDKCC_COMPILER_IDS_HPP

#include <compare>
#include <concepts>
#include <cstdint>
#include <limits>

namespace sdkcc::compiler {

template <typename Tag, std::unsigned_integral Rep = std::uint32_t>
class strong_id final {
public:
  using representation_type = Rep;
  static constexpr Rep invalid_value = std::numeric_limits<Rep>::max();

  constexpr strong_id() noexcept = default;
  explicit constexpr strong_id(Rep value) noexcept : value_{value} {}

  [[nodiscard]] constexpr Rep value() const noexcept {
    return value_;
  }
  [[nodiscard]] constexpr bool valid() const noexcept {
    return value_ != invalid_value;
  }
  explicit constexpr operator bool() const noexcept {
    return valid();
  }

  auto operator<=>(const strong_id &) const = default;

private:
  Rep value_{invalid_value};
};

struct StringTag;
struct NodeTag;
struct TypeTag;
struct EndpointTag;

using StringId = strong_id<StringTag>;
using NodeId = strong_id<NodeTag>;
using TypeId = strong_id<TypeTag>;
using EndpointId = strong_id<EndpointTag>;

} // namespace sdkcc::compiler

#endif
