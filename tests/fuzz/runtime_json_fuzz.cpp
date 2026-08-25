#include <sdkcc/sdkcc.h>

#include <cstddef>
#include <cstdint>

namespace {

sdkcc_status_t visit_value(void *, sdkcc_string_view_t,
                           sdkcc_json_value_t value, sdkcc_error_t *error) {
  switch (value.kind) {
  case SDKCC_JSON_STRING: {
    sdkcc_owned_string_t string{};
    const sdkcc_allocator_t allocator = sdkcc_v1_system_allocator();
    const sdkcc_status_t status =
        sdkcc_v1_json_value_to_owned_string(value, &allocator, &string, error);
    sdkcc_v1_owned_string_reset(&string);
    return status;
  }
  case SDKCC_JSON_NUMBER: {
    std::int64_t number{};
    return sdkcc_v1_json_value_to_i64(value, &number, error);
  }
  case SDKCC_JSON_BOOLEAN: {
    bool boolean{};
    return sdkcc_v1_json_value_to_bool(value, &boolean, error);
  }
  default:
    return SDKCC_OK;
  }
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t *data,
                                      std::size_t size) {
  sdkcc_error_t error{};
  (void)sdkcc_v1_json_object_visit({data, size}, visit_value, nullptr, &error);
  sdkcc_v1_error_reset(&error);
  return 0;
}
