#ifndef SDKCC_COMPILER_BACKEND_CODE_WRITER_HPP
#define SDKCC_COMPILER_BACKEND_CODE_WRITER_HPP

#include <sdkcc/compiler/diagnostics/diagnostic.hpp>

#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace sdkcc::compiler {

class CodeWriter final {
public:
  void line(std::string_view text = {});
  void append(std::string_view text);
  void indent() noexcept {
    ++indent_;
  }
  void dedent();
  [[nodiscard]] const std::string &str() const noexcept {
    return output_;
  }
  [[nodiscard]] std::string take() && {
    return std::move(output_);
  }

private:
  std::string output_;
  std::size_t indent_{};
  bool at_line_start_{true};
};

struct GeneratedFile {
  std::filesystem::path relative_path;
  std::string content;
};

[[nodiscard]] std::string c_string_literal(std::string_view value);
[[nodiscard]] bool write_generated_files(const std::filesystem::path &root,
                                         std::vector<GeneratedFile> files,
                                         DiagnosticBag &diagnostics);

} // namespace sdkcc::compiler

#endif
