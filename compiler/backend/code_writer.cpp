#include <sdkcc/compiler/backend/code_writer.hpp>

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <system_error>
#include <utility>

namespace sdkcc::compiler {

void CodeWriter::append(std::string_view text) {
  if (at_line_start_ && !text.empty()) {
    output_.append(indent_ * 2U, ' ');
    at_line_start_ = false;
  }
  output_.append(text);
}

void CodeWriter::line(std::string_view text) {
  append(text);
  output_.push_back('\n');
  at_line_start_ = true;
}

void CodeWriter::dedent() {
  if (indent_ == 0U) {
    throw std::logic_error{"CodeWriter indentation underflow"};
  }
  --indent_;
}

std::string c_string_literal(std::string_view value) {
  std::ostringstream output;
  output << '"';
  for (const char source_byte : value) {
    const auto byte = static_cast<unsigned char>(source_byte);
    switch (byte) {
    case '"':
      output << "\\\"";
      break;
    case '\\':
      output << "\\\\";
      break;
    case '\n':
      output << "\\n";
      break;
    case '\r':
      output << "\\r";
      break;
    case '\t':
      output << "\\t";
      break;
    default:
      if (byte < 0x20U || byte >= 0x7fU) {
        output << '\\' << std::oct << std::setw(3) << std::setfill('0')
               << static_cast<unsigned int>(byte) << std::dec;
      } else {
        output << static_cast<char>(byte);
      }
    }
  }
  output << '"';
  return output.str();
}

bool write_generated_files(const std::filesystem::path &root,
                           std::vector<GeneratedFile> files,
                           DiagnosticBag &diagnostics) {
  std::ranges::sort(files, {}, [](const GeneratedFile &file) {
    return file.relative_path.generic_string();
  });
  std::error_code filesystem_error;
  std::filesystem::create_directories(root, filesystem_error);
  if (filesystem_error) {
    diagnostics.error("E3001", "cannot create output directory: " +
                                   filesystem_error.message());
    return false;
  }
  for (const auto &file : files) {
    if (file.relative_path.empty() || file.relative_path.is_absolute()) {
      diagnostics.error("E3002", "generator produced an invalid output path");
      return false;
    }
    for (const auto &component : file.relative_path) {
      if (component == "..") {
        diagnostics.error("E3002", "generator attempted output path traversal");
        return false;
      }
    }
    const auto destination = root / file.relative_path;
    std::filesystem::create_directories(destination.parent_path(),
                                        filesystem_error);
    if (filesystem_error) {
      diagnostics.error("E3001", "cannot create generated directory: " +
                                     filesystem_error.message());
      return false;
    }
    auto temporary = destination;
    temporary += ".sdkcc.tmp";
    {
      std::ofstream stream{temporary, std::ios::binary | std::ios::trunc};
      if (!stream) {
        diagnostics.error("E3003", "cannot open generated file \"" +
                                       destination.string() + "\"");
        return false;
      }
      stream.write(file.content.data(),
                   static_cast<std::streamsize>(file.content.size()));
      stream.flush();
      if (!stream) {
        diagnostics.error("E3004", "failed writing generated file \"" +
                                       destination.string() + "\"");
        return false;
      }
    }
    std::filesystem::rename(temporary, destination, filesystem_error);
    if (filesystem_error) {
      // std::filesystem lacks replace-existing rename semantics on Windows.
      std::error_code remove_error;
      std::filesystem::remove(destination, remove_error);
      filesystem_error.clear();
      std::filesystem::rename(temporary, destination, filesystem_error);
    }
    if (filesystem_error) {
      diagnostics.error("E3005", "cannot install generated file \"" +
                                     destination.string() +
                                     "\": " + filesystem_error.message());
      return false;
    }
  }
  return true;
}

} // namespace sdkcc::compiler
