#include <sdkcc/compiler/frontend/openapi/lower.hpp>
#include <sdkcc/compiler/frontend/openapi/parser.hpp>

#include <cstddef>
#include <cstdint>
#include <span>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t *data,
                                      std::size_t size) {
  sdkcc::compiler::DiagnosticBag diagnostics;
  const auto bytes = std::span{reinterpret_cast<const std::byte *>(data), size};
  auto document = sdkcc::compiler::openapi::parse_document("fuzz.input", bytes,
                                                           diagnostics);
  if (document) {
    (void)sdkcc::compiler::openapi::lower_to_nair(**document, diagnostics);
  }
  return 0;
}
