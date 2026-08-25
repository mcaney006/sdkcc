#ifndef SDKCC_SDKCC_HPP
#define SDKCC_SDKCC_HPP

#include <sdkcc/sdkcc.h>

#include <cstdint>
#include <string>
#include <string_view>

namespace sdkcc {

struct error {
  sdkcc_status_t code{SDKCC_OK};
  sdkcc_error_category_t category{SDKCC_ERROR_NONE};
  std::int32_t http_status{};
  bool retryable{};
  std::uint64_t retry_after_ms{};
  std::string provider_code;
  std::string message;
  std::string request_id;
};

[[nodiscard]] inline std::string copy(sdkcc_string_view_t view) {
  return view.data == nullptr ? std::string{}
                              : std::string{view.data, view.len};
}

[[nodiscard]] inline error copy_error(const sdkcc_error_t &source) {
  return error{
      .code = source.code,
      .category = source.category,
      .http_status = source.http_status,
      .retryable = source.retryable,
      .retry_after_ms = source.retry_after_ms,
      .provider_code = copy(sdkcc_v1_owned_string_view(&source.provider_code)),
      .message = copy(sdkcc_v1_owned_string_view(&source.message)),
      .request_id = copy(sdkcc_v1_owned_string_view(&source.request_id)),
  };
}

class error_slot final {
public:
  error_slot() = default;
  error_slot(const error_slot &) = delete;
  error_slot &operator=(const error_slot &) = delete;
  error_slot(error_slot &&) = delete;
  error_slot &operator=(error_slot &&) = delete;
  ~error_slot() {
    sdkcc_v1_error_reset(&value_);
  }

  [[nodiscard]] sdkcc_error_t *get() noexcept {
    return &value_;
  }
  [[nodiscard]] error copy() const {
    return copy_error(value_);
  }

private:
  sdkcc_error_t value_{};
};

} // namespace sdkcc

#endif
