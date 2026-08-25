#include <sdkcc/compiler/driver.hpp>

#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#if defined(_WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif

namespace {

using sdkcc::compiler::CompileOptions;
using sdkcc::compiler::DiagnosticBag;
using sdkcc::compiler::DiagnosticFormat;

void usage(std::ostream &stream) {
  stream
      << "usage: sdkcc compile <api.yaml|api.json> [options]\n"
         "\n"
         "options:\n"
         "  --output <directory>          generated SDK root (default: "
         "./generated)\n"
         "  --lang <c|cpp|c,cpp>          output languages (default: c,cpp)\n"
         "  --library <identifier>        C library prefix\n"
         "  --namespace <identifier>      C++ namespace\n"
         "  --emit-ir                     write readable nair.txt\n"
         "  --diagnostic-format <text|json>\n";
}

bool terminal_stderr() {
  if (std::getenv("NO_COLOR") != nullptr) {
    return false;
  }
#if defined(_WIN32)
  return _isatty(_fileno(stderr)) != 0;
#else
  return isatty(STDERR_FILENO) != 0;
#endif
}

struct Arguments {
  CompileOptions options;
  DiagnosticFormat diagnostic_format{DiagnosticFormat::text};
  bool valid{};
};

Arguments parse_arguments(int argc, char **argv, DiagnosticBag &diagnostics) {
  Arguments result;
  result.options.output = "generated";
  if (argc < 3 || std::string_view{argv[1]} != "compile") {
    diagnostics.error("E0001", "expected the \"compile\" command and an input");
    return result;
  }
  result.options.input = argv[2];
  for (int index = 3; index < argc; ++index) {
    const std::string_view option = argv[index];
    auto value = [&]() -> std::string_view {
      if (index + 1 >= argc) {
        diagnostics.error("E0001", "option \"" + std::string{option} +
                                       "\" requires a value");
        return {};
      }
      ++index;
      return argv[index];
    };
    if (option == "--output") {
      result.options.output = value();
    } else if (option == "--library") {
      result.options.library = value();
    } else if (option == "--namespace") {
      result.options.cpp_namespace = value();
    } else if (option == "--lang") {
      const auto languages = value();
      if (languages == "c") {
        result.options.emit_c = true;
        result.options.emit_cpp = false;
      } else if (languages == "cpp") {
        result.options.emit_c = false;
        result.options.emit_cpp = true;
      } else if (languages == "c,cpp" || languages == "cpp,c") {
        result.options.emit_c = true;
        result.options.emit_cpp = true;
      } else {
        diagnostics.error("E0001", "invalid --lang value");
      }
    } else if (option == "--emit-ir") {
      result.options.emit_ir = true;
    } else if (option == "--diagnostic-format") {
      const auto format = value();
      if (format == "text") {
        result.diagnostic_format = DiagnosticFormat::text;
      } else if (format == "json") {
        result.diagnostic_format = DiagnosticFormat::json;
      } else {
        diagnostics.error("E0001", "invalid diagnostic format");
      }
    } else if (option == "--help" || option == "-h") {
      usage(std::cout);
      std::exit(0);
    } else {
      diagnostics.error("E0001",
                        "unknown option \"" + std::string{option} + "\"");
    }
  }
  result.valid = !diagnostics.has_errors();
  return result;
}

} // namespace

int main(int argc, char **argv) {
  DiagnosticBag diagnostics;
  const auto arguments = parse_arguments(argc, argv, diagnostics);
  if (!arguments.valid) {
    diagnostics.render(std::cerr, arguments.diagnostic_format,
                       terminal_stderr());
    usage(std::cerr);
    return 2;
  }
  const auto result = sdkcc::compiler::compile(arguments.options, diagnostics);
  if (!diagnostics.diagnostics().empty()) {
    diagnostics.render(std::cerr, arguments.diagnostic_format,
                       terminal_stderr());
  }
  if (!result) {
    return 1;
  }
  std::cout << "Generated:\n"
            << "  C23 SDK\n";
  if (arguments.options.emit_cpp) {
    std::cout << "  C++23 SDK\n";
  }
  std::cout << "  " << result->endpoints << " endpoints\n"
            << "  " << result->schemas << " models\n"
            << "  " << result->files << " files\n"
            << "Output: " << result->output.string() << '\n';
  return 0;
}
