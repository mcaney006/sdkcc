#include <sdkcc/compiler/frontend/openapi/lower.hpp>
#include <sdkcc/compiler/frontend/openapi/parser.hpp>
#include <sdkcc/compiler/naming/naming.hpp>
#include <sdkcc/compiler/passes/prepare_codegen.hpp>
#include <sdkcc/compiler/source/string_interner.hpp>

#include <cstddef>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory_resource>
#include <string>
#include <vector>

#ifndef SDKCC_MINIMAL_SPEC
#error SDKCC_MINIMAL_SPEC must name the compiler fixture
#endif

#define CHECK(expression)                                                      \
  do {                                                                         \
    if (!(expression)) {                                                       \
      std::cerr << __FILE__ << ':' << __LINE__                                 \
                << ": check failed: " #expression "\n";                        \
      return 1;                                                                \
    }                                                                          \
  } while (false)

int main() {
  using namespace sdkcc::compiler;
  std::pmr::monotonic_buffer_resource arena;
  StringInterner strings{&arena};
  const auto first = strings.intern("alpha");
  CHECK(first == strings.intern("alpha"));
  CHECK(strings.intern("beta") != first);
  CHECK(strings.get(first) == "alpha");

  CHECK(canonical_identifier("createUser", IdentifierLanguage::cpp) ==
        "create_user");
  CHECK(canonical_identifier("APIKey", IdentifierLanguage::cpp) == "api_key");
  CHECK(canonical_identifier("class", IdentifierLanguage::cpp) == "sdk_class");
  NameScope scope;
  CHECK(scope.claim("x-y", IdentifierLanguage::c) == "x_y");
  CHECK(scope.claim("x_y", IdentifierLanguage::c) == "x_y_2");

  std::ifstream input{SDKCC_MINIMAL_SPEC, std::ios::binary};
  CHECK(input.good());
  const std::string text{std::istreambuf_iterator<char>{input},
                         std::istreambuf_iterator<char>{}};
  std::vector<std::byte> bytes(text.size());
  for (std::size_t index = 0U; index < text.size(); ++index) {
    bytes[index] =
        static_cast<std::byte>(static_cast<unsigned char>(text[index]));
  }
  DiagnosticBag diagnostics;
  auto document =
      openapi::parse_document(SDKCC_MINIMAL_SPEC, bytes, diagnostics);
  CHECK(document.has_value());
  auto module = openapi::lower_to_nair(**document, diagnostics);
  CHECK(module.has_value());
  CHECK((*module)->endpoints().size() == 2U);
  CHECK((*module)->types().size() == 5U);
  auto plan = prepare_codegen(**module, "minimal", "minimal", diagnostics);
  CHECK(plan.has_value());
  CHECK(plan->emitted_endpoints.size() == 2U);
  CHECK(!diagnostics.has_errors());

  const std::string multi_document{"openapi: 3.1.0\n---\nopenapi: 3.1.0\n"};
  std::vector<std::byte> multi_bytes(multi_document.size());
  for (std::size_t index = 0U; index < multi_document.size(); ++index) {
    multi_bytes[index] = static_cast<std::byte>(
        static_cast<unsigned char>(multi_document[index]));
  }
  DiagnosticBag multi_diagnostics;
  CHECK(
      !openapi::parse_document("multiple.yaml", multi_bytes, multi_diagnostics)
           .has_value());
  CHECK(multi_diagnostics.has_errors());
  return 0;
}
