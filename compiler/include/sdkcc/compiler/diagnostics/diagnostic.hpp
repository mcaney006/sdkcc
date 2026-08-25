#ifndef SDKCC_COMPILER_DIAGNOSTICS_DIAGNOSTIC_HPP
#define SDKCC_COMPILER_DIAGNOSTICS_DIAGNOSTIC_HPP

#include <sdkcc/compiler/source_location.hpp>

#include <cstdint>
#include <optional>
#include <ostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace sdkcc::compiler {

enum class DiagnosticSeverity : std::uint8_t { note, warning, error };
enum class DiagnosticFormat : std::uint8_t { text, json };

struct RelatedDiagnostic {
  SourceRange source;
  std::string message;
};

struct Diagnostic {
  DiagnosticSeverity severity{DiagnosticSeverity::error};
  std::string code;
  std::string message;
  std::optional<SourceRange> source;
  std::string help;
  std::vector<RelatedDiagnostic> related;
};

class DiagnosticBag final {
public:
  void add(Diagnostic diagnostic);
  void error(std::string code, std::string message,
             std::optional<SourceRange> source = std::nullopt,
             std::string help = {});
  void warning(std::string code, std::string message,
               std::optional<SourceRange> source = std::nullopt,
               std::string help = {});

  [[nodiscard]] bool has_errors() const noexcept {
    return error_count_ != 0U;
  }
  [[nodiscard]] std::size_t error_count() const noexcept {
    return error_count_;
  }
  [[nodiscard]] std::span<const Diagnostic> diagnostics() const noexcept {
    return diagnostics_;
  }
  void render(std::ostream &stream, DiagnosticFormat format,
              bool color = false) const;

private:
  std::vector<Diagnostic> diagnostics_;
  std::size_t error_count_{};
};

[[nodiscard]] std::string_view
severity_name(DiagnosticSeverity severity) noexcept;

} // namespace sdkcc::compiler

#endif
