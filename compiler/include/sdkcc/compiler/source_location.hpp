#ifndef SDKCC_COMPILER_SOURCE_LOCATION_HPP
#define SDKCC_COMPILER_SOURCE_LOCATION_HPP

#include <cstddef>
#include <string>

namespace sdkcc::compiler {

struct SourceLocation {
  std::string file;
  std::size_t line{1};
  std::size_t column{1};
  std::size_t offset{};
};

struct SourceRange {
  SourceLocation begin;
  SourceLocation end;
};

} // namespace sdkcc::compiler

#endif
