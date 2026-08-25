#ifndef SDKCC_COMPILER_FRONTEND_OPENAPI_LOWER_HPP
#define SDKCC_COMPILER_FRONTEND_OPENAPI_LOWER_HPP

#include <sdkcc/compiler/diagnostics/diagnostic.hpp>
#include <sdkcc/compiler/ir/nair.hpp>
#include <sdkcc/compiler/source/document.hpp>

#include <expected>
#include <memory>

namespace sdkcc::compiler::openapi {

[[nodiscard]] std::expected<std::unique_ptr<nair::Module>, bool>
lower_to_nair(const ParsedDocument &document, DiagnosticBag &diagnostics);

} // namespace sdkcc::compiler::openapi

#endif
