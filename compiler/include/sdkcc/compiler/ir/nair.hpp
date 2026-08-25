#ifndef SDKCC_COMPILER_IR_NAIR_HPP
#define SDKCC_COMPILER_IR_NAIR_HPP

#include <sdkcc/compiler/ids.hpp>
#include <sdkcc/compiler/source/string_interner.hpp>
#include <sdkcc/compiler/source_location.hpp>

#include <memory_resource>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace sdkcc::compiler::nair {

enum class PrimitiveKind : std::uint8_t { string, integer, boolean };
enum class HttpMethod : std::uint8_t { get, post };
enum class ParameterLocation : std::uint8_t { path, query };

struct PrimitiveType {
  PrimitiveKind kind;
};

struct Field {
  StringId wire_name;
  TypeId type;
  bool required;
  SourceRange source;
};

struct StructType {
  explicit StructType(std::pmr::memory_resource *resource) : fields{resource} {}
  std::pmr::vector<Field> fields;
};

using TypeBody = std::variant<PrimitiveType, StructType>;

struct Type {
  TypeId id;
  StringId name;
  SourceRange source;
  TypeBody body;
};

struct Parameter {
  StringId wire_name;
  TypeId type;
  ParameterLocation location;
  bool required;
  SourceRange source;
};

struct RequestBody {
  TypeId type;
  bool required;
};

struct Response {
  std::int32_t http_status;
  TypeId type;
};

struct Endpoint {
  explicit Endpoint(std::pmr::memory_resource *resource)
      : parameters{resource} {}

  EndpointId id;
  StringId operation_name;
  HttpMethod method;
  StringId path;
  std::pmr::vector<Parameter> parameters;
  std::optional<RequestBody> request_body;
  Response response;
  bool authenticated{};
  SourceRange source;
};

struct ApiKeyAuth {
  StringId scheme_name;
  StringId wire_name;
};

class Module final {
public:
  Module();
  Module(const Module &) = delete;
  Module &operator=(const Module &) = delete;

  [[nodiscard]] StringId intern(std::string_view value) {
    return strings_.intern(value);
  }
  [[nodiscard]] std::string_view string(StringId id) const {
    return strings_.get(id);
  }

  [[nodiscard]] TypeId add_primitive(std::string_view name, PrimitiveKind kind);
  [[nodiscard]] TypeId add_struct(std::string_view name, SourceRange source);
  [[nodiscard]] EndpointId add_endpoint(Endpoint endpoint);

  [[nodiscard]] Type &type_mut(TypeId id);
  [[nodiscard]] const Type &type(TypeId id) const;
  [[nodiscard]] Endpoint &endpoint_mut(EndpointId id);
  [[nodiscard]] const Endpoint &endpoint(EndpointId id) const;
  [[nodiscard]] std::span<const Type> types() const noexcept {
    return types_;
  }
  [[nodiscard]] std::span<const Endpoint> endpoints() const noexcept {
    return endpoints_;
  }
  [[nodiscard]] std::pmr::memory_resource *resource() noexcept {
    return &arena_;
  }

  StringId name;
  StringId base_url;
  std::optional<ApiKeyAuth> api_key_auth;

private:
  std::pmr::monotonic_buffer_resource arena_;
  StringInterner strings_;
  std::pmr::vector<Type> types_;
  std::pmr::vector<Endpoint> endpoints_;
};

[[nodiscard]] std::string dump(const Module &module);
[[nodiscard]] std::string_view method_name(HttpMethod method) noexcept;
[[nodiscard]] std::string_view primitive_name(PrimitiveKind kind) noexcept;

} // namespace sdkcc::compiler::nair

#endif
