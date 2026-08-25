#include <sdkcc/compiler/naming/naming.hpp>

#include <algorithm>
#include <array>
#include <cctype>

namespace sdkcc::compiler {
namespace {

constexpr auto c_keywords = std::to_array<std::string_view>({
    "alignas",      "alignof",  "auto",          "bool",        "break",
    "case",         "char",     "const",         "constexpr",   "continue",
    "default",      "do",       "double",        "else",        "enum",
    "extern",       "false",    "float",         "for",         "goto",
    "if",           "inline",   "int",           "long",        "nullptr",
    "register",     "restrict", "return",        "short",       "signed",
    "sizeof",       "static",   "static_assert", "struct",      "switch",
    "thread_local", "true",     "typedef",       "typeof",      "typeof_unqual",
    "union",        "unsigned", "void",          "volatile",    "while",
    "_Atomic",      "_BitInt",  "_Complex",      "_Decimal128", "_Decimal32",
    "_Decimal64",   "_Generic", "_Imaginary",    "_Noreturn",
});

constexpr auto cpp_only_keywords = std::to_array<std::string_view>({
    "and",          "and_eq",
    "asm",          "bitand",
    "bitor",        "catch",
    "char8_t",      "char16_t",
    "char32_t",     "class",
    "compl",        "concept",
    "const_cast",   "consteval",
    "constinit",    "co_await",
    "co_return",    "co_yield",
    "decltype",     "delete",
    "dynamic_cast", "explicit",
    "export",       "friend",
    "mutable",      "namespace",
    "new",          "noexcept",
    "not",          "not_eq",
    "operator",     "or",
    "or_eq",        "private",
    "protected",    "public",
    "reflexpr",     "reinterpret_cast",
    "requires",     "static_cast",
    "template",     "this",
    "throw",        "try",
    "typeid",       "typename",
    "using",        "virtual",
    "wchar_t",      "xor",
    "xor_eq",
});

bool keyword(std::string_view value, IdentifierLanguage language) {
  const auto contains = [value](const auto &keywords) {
    return std::ranges::find(keywords, value) != keywords.end();
  };
  return contains(c_keywords) ||
         (language == IdentifierLanguage::cpp && contains(cpp_only_keywords));
}

} // namespace

std::string canonical_identifier(std::string_view source,
                                 IdentifierLanguage language) {
  std::string result;
  result.reserve(source.size() + 4U);
  bool separator = false;
  for (std::size_t index = 0U; index < source.size(); ++index) {
    const char source_byte = source[index];
    const auto byte = static_cast<unsigned char>(source_byte);
    if (std::isalnum(byte) != 0) {
      if (separator && !result.empty() && result.back() != '_') {
        result.push_back('_');
      }
      const bool uppercase = std::isupper(byte) != 0;
      const bool previous_word =
          index != 0U &&
          (std::islower(static_cast<unsigned char>(source[index - 1U])) != 0 ||
           std::isdigit(static_cast<unsigned char>(source[index - 1U])) != 0);
      const bool acronym_boundary =
          uppercase && index + 1U < source.size() &&
          std::islower(static_cast<unsigned char>(source[index + 1U])) != 0 &&
          index != 0U &&
          std::isupper(static_cast<unsigned char>(source[index - 1U])) != 0;
      if (uppercase && (previous_word || acronym_boundary) && !result.empty() &&
          result.back() != '_') {
        result.push_back('_');
      }
      result.push_back(static_cast<char>(std::tolower(byte)));
      separator = false;
    } else {
      separator = true;
    }
  }
  while (!result.empty() && result.back() == '_') {
    result.pop_back();
  }
  if (result.empty()) {
    result = "unnamed";
  }
  if (std::isdigit(static_cast<unsigned char>(result.front())) != 0) {
    result.insert(0U, "n_");
  }
  // Any leading underscore can enter an implementation-reserved namespace.
  if (result.front() == '_' || keyword(result, language)) {
    result.insert(0U, "sdk_");
  }
  return result;
}

std::string NameScope::claim(std::string_view source,
                             IdentifierLanguage language) {
  auto base = canonical_identifier(source, language);
  auto [entry, inserted] = claims_.try_emplace(base, 1U);
  if (inserted) {
    return base;
  }
  ++entry->second;
  return base + '_' + std::to_string(entry->second);
}

} // namespace sdkcc::compiler
