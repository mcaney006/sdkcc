#ifndef SDKCC_TEST_TRANSPORT_H
#define SDKCC_TEST_TRANSPORT_H

#include <sdkcc/sdkcc.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct sdkcc_test_transport sdkcc_test_transport_t;

typedef struct sdkcc_test_header {
  sdkcc_string_view_t name;
  sdkcc_string_view_t value;
} sdkcc_test_header_t;

typedef struct sdkcc_test_expectation {
  sdkcc_http_method_t method;
  sdkcc_string_view_t url;
  const sdkcc_test_header_t *headers;
  size_t header_count;
  sdkcc_buffer_view_t body;
  int32_t response_status;
  sdkcc_buffer_view_t response_body;
} sdkcc_test_expectation_t;

SDKCC_API sdkcc_status_t sdkcc_v1_test_transport_create(
    const sdkcc_allocator_t *allocator, sdkcc_test_transport_t **out_transport,
    sdkcc_error_t *error);
SDKCC_API void
sdkcc_v1_test_transport_destroy(sdkcc_test_transport_t *transport);
SDKCC_API sdkcc_transport_t
sdkcc_v1_test_transport_as_transport(sdkcc_test_transport_t *transport);
SDKCC_API sdkcc_status_t sdkcc_v1_test_transport_expect(
    sdkcc_test_transport_t *transport,
    const sdkcc_test_expectation_t *expectation, sdkcc_error_t *error);
SDKCC_API sdkcc_status_t sdkcc_v1_test_transport_verify(
    const sdkcc_test_transport_t *transport, sdkcc_error_t *error);

#ifdef __cplusplus
}
#endif

#endif
