#include <sdkcc/compiler/diagnostics/diagnostic.hpp>

#include <iomanip>
#include <span>

namespace sdkcc::compiler {
namespace {

void write_json_string(std::ostream &stream, std::string_view value) {
  stream << '"';
  for (const char source_byte : value) {
    const auto byte = static_cast<unsigned char>(source_byte);
    switch (byte) {
    case '"':
      stream << "\\\"";
      break;
    case '\\':
      stream << "\\\\";
      break;
    case '\b':
      stream << "\\b";
      break;
    case '\f':
      stream << "\\f";
      break;
    case '\n':
      stream << "\\n";
      break;
    case '\r':
      stream << "\\r";
      break;
    case '\t':
      stream << "\\t";
      break;
    default:
      if (byte < 0x20U) {
        const auto flags = stream.flags();
        const auto fill = stream.fill();
        stream << "\\u" << std::hex << std::uppercase << std::setw(4)
               << std::setfill('0') << static_cast<unsigned int>(byte);
        stream.flags(flags);
        stream.fill(fill);
      } else {
        stream << static_cast<char>(byte);
      }
    }
  }
  stream << '"';
}

void render_location_json(std::ostream &stream, const SourceRange &source) {
  stream << "{\"file\":";
  write_json_string(stream, source.begin.file);
  stream << ",\"line\":" << source.begin.line
         << ",\"column\":" << source.begin.column
         << ",\"end_line\":" << source.end.line
         << ",\"end_column\":" << source.end.column << '}';
}

} // namespace

std::string_view severity_name(DiagnosticSeverity severity) noexcept {
  switch (severity) {
  case DiagnosticSeverity::note:
    return "note";
  case DiagnosticSeverity::warning:
    return "warning";
  case DiagnosticSeverity::error:
    return "error";
  }
  return "error";
}

void DiagnosticBag::add(Diagnostic diagnostic) {
  if (diagnostic.severity == DiagnosticSeverity::error) {
    ++error_count_;
  }
  diagnostics_.push_back(std::move(diagnostic));
}

void DiagnosticBag::error(std::string code, std::string message,
                          std::optional<SourceRange> source, std::string help) {
  add(Diagnostic{.severity = DiagnosticSeverity::error,
                 .code = std::move(code),
                 .message = std::move(message),
                 .source = std::move(source),
                 .help = std::move(help),
                 .related = {}});
}

void DiagnosticBag::warning(std::string code, std::string message,
                            std::optional<SourceRange> source,
                            std::string help) {
  add(Diagnostic{.severity = DiagnosticSeverity::warning,
                 .code = std::move(code),
                 .message = std::move(message),
                 .source = std::move(source),
                 .help = std::move(help),
                 .related = {}});
}

void DiagnosticBag::render(std::ostream &stream, DiagnosticFormat format,
                           bool color) const {
  if (format == DiagnosticFormat::json) {
    stream << '[';
    for (std::size_t index = 0U; index < diagnostics_.size(); ++index) {
      if (index != 0U) {
        stream << ',';
      }
      const auto &diagnostic = diagnostics_[index];
      stream << "{\"severity\":";
      write_json_string(stream, severity_name(diagnostic.severity));
      stream << ",\"code\":";
      write_json_string(stream, diagnostic.code);
      stream << ",\"message\":";
      write_json_string(stream, diagnostic.message);
      stream << ",\"source\":";
      if (diagnostic.source) {
        render_location_json(stream, *diagnostic.source);
      } else {
        stream << "null";
      }
      stream << ",\"help\":";
      write_json_string(stream, diagnostic.help);
      stream << '}';
    }
    stream << "]\n";
    return;
  }

  for (const auto &diagnostic : diagnostics_) {
    if (diagnostic.source) {
      stream << diagnostic.source->begin.file << ':'
             << diagnostic.source->begin.line << ':'
             << diagnostic.source->begin.column << ": ";
    }
    if (color) {
      const char *const code =
          diagnostic.severity == DiagnosticSeverity::error     ? "\x1b[31;1m"
          : diagnostic.severity == DiagnosticSeverity::warning ? "\x1b[33;1m"
                                                               : "\x1b[36;1m";
      stream << code;
    }
    stream << severity_name(diagnostic.severity) << ' ' << diagnostic.code;
    if (color) {
      stream << "\x1b[0m";
    }
    stream << ": " << diagnostic.message << '\n';
    if (!diagnostic.help.empty()) {
      stream << "  help: " << diagnostic.help << '\n';
    }
    for (const auto &related : diagnostic.related) {
      stream << "  note: " << related.source.begin.file << ':'
             << related.source.begin.line << ':' << related.source.begin.column
             << ": " << related.message << '\n';
    }
  }
}

} // namespace sdkcc::compiler
