#include <sdkcc/sdkcc.h>

#include <stdio.h>
#include <string.h>

#define CHECK(expression)                                                      \
  do {                                                                         \
    if (!(expression)) {                                                       \
      fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__,         \
              #expression);                                                    \
      return 1;                                                                \
    }                                                                          \
  } while (false)

typedef struct parsed_values {
  sdkcc_allocator_t allocator;
  sdkcc_owned_string_t text;
  int64_t number;
  bool flag;
  uint8_t seen;
} parsed_values_t;

static sdkcc_status_t visit_member(void *opaque, sdkcc_string_view_t key,
                                   sdkcc_json_value_t value,
                                   sdkcc_error_t *error) {
  parsed_values_t *const parsed = opaque;
  if (sdkcc_v1_json_key_equals(key, SDKCC_STR_LITERAL("text"))) {
    parsed->seen |= UINT8_C(1);
    return sdkcc_v1_json_value_to_owned_string(value, &parsed->allocator,
                                               &parsed->text, error);
  }
  if (sdkcc_v1_json_key_equals(key, SDKCC_STR_LITERAL("number"))) {
    parsed->seen |= UINT8_C(2);
    return sdkcc_v1_json_value_to_i64(value, &parsed->number, error);
  }
  if (sdkcc_v1_json_key_equals(key, SDKCC_STR_LITERAL("flag"))) {
    parsed->seen |= UINT8_C(4);
    return sdkcc_v1_json_value_to_bool(value, &parsed->flag, error);
  }
  return SDKCC_OK;
}

int main(void) {
  sdkcc_error_t error = {0};
  const sdkcc_allocator_t allocator = sdkcc_v1_system_allocator();
  CHECK(sdkcc_v1_allocator_is_valid(&allocator));
  void *const aligned = allocator.alloc(allocator.ctx, 17U, 64U);
  CHECK(aligned != NULL);
  CHECK(((uintptr_t)aligned & (uintptr_t)63U) == 0U);
  allocator.free(allocator.ctx, aligned, 17U, 64U);

  sdkcc_buffer_t encoded = {0};
  CHECK(sdkcc_v1_buffer_init(&encoded, &allocator) == SDKCC_OK);
  CHECK(sdkcc_v1_uri_encode_append(&encoded, SDKCC_STR_LITERAL("a/b c"),
                                   &error) == SDKCC_OK);
  CHECK(encoded.len == 9U);
  CHECK(memcmp(encoded.data, "a%2Fb%20c", encoded.len) == 0);
  sdkcc_v1_buffer_reset(&encoded);

  static const char json[] =
      "{\"text\":\"A\\u00df\\ud83d\\ude80\",\"number\":-9223372036854775808,"
      "\"flag\":true,\"ignored\":[1,{\"x\":null}]}";
  parsed_values_t parsed = {.allocator = allocator};
  CHECK(sdkcc_v1_json_object_visit(SDKCC_BYTES_LITERAL(json), visit_member,
                                   &parsed, &error) == SDKCC_OK);
  CHECK(parsed.seen == UINT8_C(7));
  CHECK(parsed.number == INT64_MIN);
  CHECK(parsed.flag);
  CHECK(parsed.text.len == strlen("Aß🚀"));
  CHECK(memcmp(parsed.text.data, "Aß🚀", parsed.text.len) == 0);
  sdkcc_v1_owned_string_reset(&parsed.text);

  static const char invalid[] = "{\"x\":\"\\ud800\"}";
  CHECK(sdkcc_v1_json_object_visit(SDKCC_BYTES_LITERAL(invalid), visit_member,
                                   &parsed, &error) == SDKCC_ERR_PARSE);
  sdkcc_v1_error_reset(&error);
  return 0;
}
