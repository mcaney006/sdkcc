#ifndef SDKCC_COMPILER_NAMING_NAMING_HPP
#define SDKCC_COMPILER_NAMING_NAMING_HPP

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>

namespace sdkcc::compiler {

enum class IdentifierLanguage : std::uint8_t { c, cpp };

[[nodiscard]] std::string canonical_identifier(std::string_view source,
                                               IdentifierLanguage language);

class NameScope final {
public:
  [[nodiscard]] std::string claim(std::string_view source,
                                  IdentifierLanguage language);

private:
  std::unordered_map<std::string, std::size_t> claims_;
};

} // namespace sdkcc::compiler

#endif
