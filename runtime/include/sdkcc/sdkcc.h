#ifndef SDKCC_SDKCC_H
#define SDKCC_SDKCC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#if defined(SDKCC_STATIC)
#define SDKCC_API
#elif defined(_WIN32)
#if defined(SDKCC_BUILDING_RUNTIME)
#define SDKCC_API __declspec(dllexport)
#else
#define SDKCC_API __declspec(dllimport)
#endif
#elif defined(__GNUC__) || defined(__clang__)
#define SDKCC_API __attribute__((visibility("default")))
#else
#define SDKCC_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define SDKCC_ABI_VERSION UINT32_C(1)
#define SDKCC_JSON_MAX_DEPTH UINT32_C(64)

typedef enum sdkcc_status {
  SDKCC_OK = 0,
  SDKCC_ERR_INVALID_ARGUMENT = 1,
  SDKCC_ERR_OOM = 2,
  SDKCC_ERR_OVERFLOW = 3,
  SDKCC_ERR_NETWORK = 4,
  SDKCC_ERR_DNS = 5,
  SDKCC_ERR_TLS = 6,
  SDKCC_ERR_TIMEOUT = 7,
  SDKCC_ERR_CANCELLED = 8,
  SDKCC_ERR_HTTP = 9,
  SDKCC_ERR_AUTH = 10,
  SDKCC_ERR_RATE_LIMIT = 11,
  SDKCC_ERR_SERIALIZE = 12,
  SDKCC_ERR_PARSE = 13,
  SDKCC_ERR_SCHEMA = 14,
  SDKCC_ERR_INTERNAL = 15,
  SDKCC_ERR_TEST_MISMATCH = 16
} sdkcc_status_t;

typedef enum sdkcc_error_category {
  SDKCC_ERROR_NONE = 0,
  SDKCC_ERROR_ARGUMENT = 1,
  SDKCC_ERROR_MEMORY = 2,
  SDKCC_ERROR_TRANSPORT = 3,
  SDKCC_ERROR_PROTOCOL = 4,
  SDKCC_ERROR_SERIALIZATION = 5,
  SDKCC_ERROR_TEST = 6,
  SDKCC_ERROR_INTERNAL = 7
} sdkcc_error_category_t;

typedef struct sdkcc_string_view {
  const char *data;
  size_t len;
} sdkcc_string_view_t;

typedef struct sdkcc_buffer_view {
  const uint8_t *data;
  size_t len;
} sdkcc_buffer_view_t;

typedef void *(*sdkcc_alloc_fn)(void *ctx, size_t size, size_t alignment);
typedef void *(*sdkcc_realloc_fn)(void *ctx, void *ptr, size_t old_size,
                                  size_t new_size, size_t alignment);
typedef void (*sdkcc_free_fn)(void *ctx, void *ptr, size_t size,
                              size_t alignment);

typedef struct sdkcc_allocator {
  void *ctx;
  sdkcc_alloc_fn alloc;
  sdkcc_realloc_fn realloc;
  sdkcc_free_fn free;
} sdkcc_allocator_t;

typedef struct sdkcc_owned_string {
  char *data;
  size_t len;
  sdkcc_allocator_t allocator;
} sdkcc_owned_string_t;

typedef struct sdkcc_buffer {
  uint8_t *data;
  size_t len;
  size_t capacity;
  sdkcc_allocator_t allocator;
} sdkcc_buffer_t;

typedef struct sdkcc_error {
  sdkcc_error_category_t category;
  sdkcc_status_t code;
  int32_t http_status;
  bool retryable;
  uint64_t retry_after_ms;
  sdkcc_owned_string_t provider_code;
  sdkcc_owned_string_t message;
  sdkcc_owned_string_t request_id;
} sdkcc_error_t;

SDKCC_API sdkcc_allocator_t sdkcc_v1_system_allocator(void);
SDKCC_API bool sdkcc_v1_allocator_is_valid(const sdkcc_allocator_t *allocator);
SDKCC_API sdkcc_string_view_t sdkcc_v1_string_view(const char *text);
SDKCC_API sdkcc_string_view_t
sdkcc_v1_owned_string_view(const sdkcc_owned_string_t *string);
SDKCC_API sdkcc_buffer_view_t
sdkcc_v1_buffer_view(const sdkcc_buffer_t *buffer);
SDKCC_API void sdkcc_v1_owned_string_reset(sdkcc_owned_string_t *string);
SDKCC_API void sdkcc_v1_buffer_reset(sdkcc_buffer_t *buffer);
SDKCC_API void sdkcc_v1_error_reset(sdkcc_error_t *error);
SDKCC_API sdkcc_status_t sdkcc_v1_error_set(sdkcc_error_t *error,
                                            sdkcc_status_t code,
                                            sdkcc_error_category_t category,
                                            const char *message);
SDKCC_API sdkcc_status_t sdkcc_v1_owned_string_copy(
    sdkcc_string_view_t source, const sdkcc_allocator_t *allocator,
    sdkcc_owned_string_t *out, sdkcc_error_t *error);

#define SDKCC_STR(text) sdkcc_v1_string_view((text))
#define SDKCC_STR_LITERAL(text)                                                \
  ((sdkcc_string_view_t){(text), sizeof(text) - 1U})
#define SDKCC_BYTES_LITERAL(text)                                              \
  ((sdkcc_buffer_view_t){(const uint8_t *)(text), sizeof(text) - 1U})

SDKCC_API
sdkcc_status_t sdkcc_v1_buffer_init(sdkcc_buffer_t *buffer,
                                    const sdkcc_allocator_t *allocator);
SDKCC_API sdkcc_status_t sdkcc_v1_buffer_reserve(sdkcc_buffer_t *buffer,
                                                 size_t capacity,
                                                 sdkcc_error_t *error);
SDKCC_API sdkcc_status_t sdkcc_v1_buffer_append(sdkcc_buffer_t *buffer,
                                                sdkcc_buffer_view_t value,
                                                sdkcc_error_t *error);
SDKCC_API sdkcc_status_t sdkcc_v1_buffer_append_string(
    sdkcc_buffer_t *buffer, sdkcc_string_view_t value, sdkcc_error_t *error);
SDKCC_API sdkcc_status_t sdkcc_v1_buffer_append_byte(sdkcc_buffer_t *buffer,
                                                     uint8_t value,
                                                     sdkcc_error_t *error);
SDKCC_API sdkcc_status_t sdkcc_v1_uri_encode_append(sdkcc_buffer_t *buffer,
                                                    sdkcc_string_view_t value,
                                                    sdkcc_error_t *error);

typedef enum sdkcc_http_method {
  SDKCC_HTTP_INVALID = 0,
  SDKCC_HTTP_GET = 1,
  SDKCC_HTTP_POST = 2,
  SDKCC_HTTP_PUT = 3,
  SDKCC_HTTP_PATCH = 4,
  SDKCC_HTTP_DELETE = 5,
  SDKCC_HTTP_HEAD = 6
} sdkcc_http_method_t;

typedef struct sdkcc_request sdkcc_request_t;

typedef struct sdkcc_header_view {
  sdkcc_string_view_t name;
  sdkcc_string_view_t value;
} sdkcc_header_view_t;

typedef struct sdkcc_response {
  int32_t http_status;
  sdkcc_buffer_t body;
} sdkcc_response_t;

SDKCC_API sdkcc_status_t
sdkcc_v1_request_create(const sdkcc_allocator_t *allocator,
                        sdkcc_request_t **out_request, sdkcc_error_t *error);
SDKCC_API void sdkcc_v1_request_destroy(sdkcc_request_t *request);
SDKCC_API sdkcc_status_t sdkcc_v1_request_set_method(sdkcc_request_t *request,
                                                     sdkcc_http_method_t method,
                                                     sdkcc_error_t *error);
SDKCC_API sdkcc_status_t sdkcc_v1_request_set_url(sdkcc_request_t *request,
                                                  sdkcc_string_view_t url,
                                                  sdkcc_error_t *error);
SDKCC_API sdkcc_status_t sdkcc_v1_request_add_header(sdkcc_request_t *request,
                                                     sdkcc_string_view_t name,
                                                     sdkcc_string_view_t value,
                                                     sdkcc_error_t *error);
SDKCC_API sdkcc_status_t sdkcc_v1_request_set_body(sdkcc_request_t *request,
                                                   sdkcc_buffer_view_t body,
                                                   sdkcc_error_t *error);
SDKCC_API sdkcc_http_method_t
sdkcc_v1_request_method(const sdkcc_request_t *request);
SDKCC_API sdkcc_string_view_t
sdkcc_v1_request_url(const sdkcc_request_t *request);
SDKCC_API sdkcc_buffer_view_t
sdkcc_v1_request_body(const sdkcc_request_t *request);
SDKCC_API size_t sdkcc_v1_request_header_count(const sdkcc_request_t *request);
SDKCC_API sdkcc_status_t
sdkcc_v1_request_header_at(const sdkcc_request_t *request, size_t index,
                           sdkcc_header_view_t *out_header);
SDKCC_API void sdkcc_v1_response_reset(sdkcc_response_t *response);

typedef sdkcc_status_t (*sdkcc_transport_send_fn)(
    void *ctx, const sdkcc_request_t *request,
    const sdkcc_allocator_t *response_allocator, sdkcc_response_t *out_response,
    sdkcc_error_t *error);

typedef struct sdkcc_transport_vtable {
  uint32_t abi_version;
  uint32_t struct_size;
  sdkcc_transport_send_fn send;
} sdkcc_transport_vtable_t;

typedef struct sdkcc_transport {
  void *ctx;
  const sdkcc_transport_vtable_t *vtable;
  uint64_t capabilities;
} sdkcc_transport_t;

SDKCC_API sdkcc_status_t sdkcc_v1_transport_send(
    const sdkcc_transport_t *transport, const sdkcc_request_t *request,
    const sdkcc_allocator_t *response_allocator, sdkcc_response_t *out_response,
    sdkcc_error_t *error);

typedef enum sdkcc_json_kind {
  SDKCC_JSON_NULL = 0,
  SDKCC_JSON_BOOLEAN = 1,
  SDKCC_JSON_NUMBER = 2,
  SDKCC_JSON_STRING = 3,
  SDKCC_JSON_ARRAY = 4,
  SDKCC_JSON_OBJECT = 5
} sdkcc_json_kind_t;

typedef struct sdkcc_json_value {
  sdkcc_json_kind_t kind;
  sdkcc_buffer_view_t raw;
} sdkcc_json_value_t;

typedef sdkcc_status_t (*sdkcc_json_member_fn)(void *ctx,
                                               sdkcc_string_view_t raw_key,
                                               sdkcc_json_value_t value,
                                               sdkcc_error_t *error);

SDKCC_API sdkcc_status_t sdkcc_v1_json_object_visit(
    sdkcc_buffer_view_t json, sdkcc_json_member_fn visitor, void *visitor_ctx,
    sdkcc_error_t *error);
SDKCC_API bool sdkcc_v1_json_key_equals(sdkcc_string_view_t raw_key,
                                        sdkcc_string_view_t expected);
SDKCC_API sdkcc_status_t sdkcc_v1_json_value_to_owned_string(
    sdkcc_json_value_t value, const sdkcc_allocator_t *allocator,
    sdkcc_owned_string_t *out_string, sdkcc_error_t *error);
SDKCC_API sdkcc_status_t sdkcc_v1_json_value_to_i64(sdkcc_json_value_t value,
                                                    int64_t *out_value,
                                                    sdkcc_error_t *error);
SDKCC_API sdkcc_status_t sdkcc_v1_json_value_to_bool(sdkcc_json_value_t value,
                                                     bool *out_value,
                                                     sdkcc_error_t *error);
SDKCC_API sdkcc_status_t sdkcc_v1_json_write_string(sdkcc_buffer_t *buffer,
                                                    sdkcc_string_view_t value,
                                                    sdkcc_error_t *error);
SDKCC_API sdkcc_status_t sdkcc_v1_json_write_i64(sdkcc_buffer_t *buffer,
                                                 int64_t value,
                                                 sdkcc_error_t *error);
SDKCC_API sdkcc_status_t sdkcc_v1_json_write_bool(sdkcc_buffer_t *buffer,
                                                  bool value,
                                                  sdkcc_error_t *error);

#ifdef __cplusplus
}
#endif

#endif
