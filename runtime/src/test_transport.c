#include "internal.h"

#include <sdkcc/test_transport.h>

#include <string.h>

typedef struct sdkcc_owned_test_header {
  sdkcc_owned_string_t name;
  sdkcc_owned_string_t value;
} sdkcc_owned_test_header_t;

typedef struct sdkcc_owned_expectation {
  sdkcc_http_method_t method;
  sdkcc_owned_string_t url;
  sdkcc_owned_test_header_t *headers;
  size_t header_count;
  sdkcc_buffer_t body;
  int32_t response_status;
  sdkcc_buffer_t response_body;
} sdkcc_owned_expectation_t;

struct sdkcc_test_transport {
  sdkcc_allocator_t allocator;
  sdkcc_owned_expectation_t *expectations;
  size_t expectation_count;
  size_t expectation_capacity;
  size_t next_expectation;
};

static bool strings_equal(sdkcc_string_view_t left, sdkcc_string_view_t right) {
  return left.len == right.len &&
         (left.len == 0U || memcmp(left.data, right.data, left.len) == 0);
}

static bool buffers_equal(sdkcc_buffer_view_t left, sdkcc_buffer_view_t right) {
  return left.len == right.len &&
         (left.len == 0U || memcmp(left.data, right.data, left.len) == 0);
}

static void expectation_reset(sdkcc_owned_expectation_t *expectation,
                              const sdkcc_allocator_t *allocator) {
  sdkcc_v1_owned_string_reset(&expectation->url);
  sdkcc_v1_buffer_reset(&expectation->body);
  sdkcc_v1_buffer_reset(&expectation->response_body);
  for (size_t index = 0U; index < expectation->header_count; ++index) {
    sdkcc_v1_owned_string_reset(&expectation->headers[index].name);
    sdkcc_v1_owned_string_reset(&expectation->headers[index].value);
  }
  if (expectation->headers != NULL) {
    size_t bytes = 0U;
    if (sdkcc_internal_checked_mul(expectation->header_count,
                                   sizeof(*expectation->headers), &bytes)) {
      allocator->free(allocator->ctx, expectation->headers, bytes,
                      alignof(sdkcc_owned_test_header_t));
    }
  }
  *expectation = (sdkcc_owned_expectation_t){0};
}

static sdkcc_status_t test_send(void *ctx, const sdkcc_request_t *request,
                                const sdkcc_allocator_t *response_allocator,
                                sdkcc_response_t *out_response,
                                sdkcc_error_t *error) {
  sdkcc_test_transport_t *const transport = ctx;
  if (transport == NULL ||
      transport->next_expectation >= transport->expectation_count) {
    return sdkcc_internal_error_set(error, SDKCC_ERR_TEST_MISMATCH,
                                    SDKCC_ERROR_TEST,
                                    "unexpected transport request");
  }
  const sdkcc_owned_expectation_t *const expected =
      &transport->expectations[transport->next_expectation];
  if (sdkcc_v1_request_method(request) != expected->method ||
      !strings_equal(sdkcc_v1_request_url(request),
                     sdkcc_v1_owned_string_view(&expected->url)) ||
      !buffers_equal(sdkcc_v1_request_body(request),
                     sdkcc_v1_buffer_view(&expected->body))) {
    return sdkcc_internal_error_set(error, SDKCC_ERR_TEST_MISMATCH,
                                    SDKCC_ERROR_TEST,
                                    "request method, URL, or body mismatch");
  }

  for (size_t wanted = 0U; wanted < expected->header_count; ++wanted) {
    bool found = false;
    for (size_t actual = 0U; actual < sdkcc_v1_request_header_count(request);
         ++actual) {
      sdkcc_header_view_t header = {0};
      if (sdkcc_v1_request_header_at(request, actual, &header) == SDKCC_OK &&
          strings_equal(header.name, sdkcc_v1_owned_string_view(
                                         &expected->headers[wanted].name)) &&
          strings_equal(header.value, sdkcc_v1_owned_string_view(
                                          &expected->headers[wanted].value))) {
        found = true;
        break;
      }
    }
    if (!found) {
      return sdkcc_internal_error_set(error, SDKCC_ERR_TEST_MISMATCH,
                                      SDKCC_ERROR_TEST,
                                      "required request header mismatch");
    }
  }

  out_response->http_status = expected->response_status;
  const sdkcc_status_t status = sdkcc_internal_buffer_copy(
      sdkcc_v1_buffer_view(&expected->response_body), response_allocator,
      &out_response->body, error);
  if (status != SDKCC_OK) {
    return status;
  }
  ++transport->next_expectation;
  return SDKCC_OK;
}

static const sdkcc_transport_vtable_t test_vtable = {
    .abi_version = SDKCC_ABI_VERSION,
    .struct_size = sizeof(sdkcc_transport_vtable_t),
    .send = test_send,
};

sdkcc_status_t
sdkcc_v1_test_transport_create(const sdkcc_allocator_t *allocator,
                               sdkcc_test_transport_t **out_transport,
                               sdkcc_error_t *error) {
  if (out_transport == NULL) {
    return sdkcc_internal_error_set(error, SDKCC_ERR_INVALID_ARGUMENT,
                                    SDKCC_ERROR_ARGUMENT,
                                    "test transport output is null");
  }
  *out_transport = NULL;
  const sdkcc_allocator_t selected =
      sdkcc_internal_allocator_or_system(allocator);
  if (!sdkcc_v1_allocator_is_valid(&selected)) {
    return sdkcc_internal_error_set(error, SDKCC_ERR_INVALID_ARGUMENT,
                                    SDKCC_ERROR_ARGUMENT,
                                    "test transport allocator is invalid");
  }
  sdkcc_test_transport_t *const transport = selected.alloc(
      selected.ctx, sizeof(*transport), alignof(sdkcc_test_transport_t));
  if (transport == NULL) {
    return sdkcc_internal_error_set(error, SDKCC_ERR_OOM, SDKCC_ERROR_MEMORY,
                                    "test transport allocation failed");
  }
  *transport = (sdkcc_test_transport_t){.allocator = selected};
  *out_transport = transport;
  return SDKCC_OK;
}

void sdkcc_v1_test_transport_destroy(sdkcc_test_transport_t *transport) {
  if (transport == NULL) {
    return;
  }
  const sdkcc_allocator_t allocator = transport->allocator;
  for (size_t index = 0U; index < transport->expectation_count; ++index) {
    expectation_reset(&transport->expectations[index], &allocator);
  }
  if (transport->expectations != NULL) {
    size_t bytes = 0U;
    if (sdkcc_internal_checked_mul(transport->expectation_capacity,
                                   sizeof(*transport->expectations), &bytes)) {
      allocator.free(allocator.ctx, transport->expectations, bytes,
                     alignof(sdkcc_owned_expectation_t));
    }
  }
  allocator.free(allocator.ctx, transport, sizeof(*transport),
                 alignof(sdkcc_test_transport_t));
}

sdkcc_transport_t
sdkcc_v1_test_transport_as_transport(sdkcc_test_transport_t *transport) {
  return (sdkcc_transport_t){
      .ctx = transport, .vtable = &test_vtable, .capabilities = 0U};
}

static sdkcc_status_t reserve_expectations(sdkcc_test_transport_t *transport,
                                           size_t required,
                                           sdkcc_error_t *error) {
  if (required <= transport->expectation_capacity) {
    return SDKCC_OK;
  }
  size_t next = transport->expectation_capacity == 0U
                    ? 4U
                    : transport->expectation_capacity;
  while (next < required) {
    if (next > SIZE_MAX / 2U) {
      next = required;
      break;
    }
    next *= 2U;
  }
  size_t old_bytes = 0U;
  size_t new_bytes = 0U;
  if (!sdkcc_internal_checked_mul(transport->expectation_capacity,
                                  sizeof(*transport->expectations),
                                  &old_bytes) ||
      !sdkcc_internal_checked_mul(next, sizeof(*transport->expectations),
                                  &new_bytes)) {
    return sdkcc_internal_error_set(error, SDKCC_ERR_OVERFLOW,
                                    SDKCC_ERROR_MEMORY,
                                    "expectation array size overflow");
  }
  void *const memory = transport->allocator.realloc(
      transport->allocator.ctx, transport->expectations, old_bytes, new_bytes,
      alignof(sdkcc_owned_expectation_t));
  if (memory == NULL) {
    return sdkcc_internal_error_set(error, SDKCC_ERR_OOM, SDKCC_ERROR_MEMORY,
                                    "expectation array allocation failed");
  }
  transport->expectations = memory;
  for (size_t index = transport->expectation_capacity; index < next; ++index) {
    transport->expectations[index] = (sdkcc_owned_expectation_t){0};
  }
  transport->expectation_capacity = next;
  return SDKCC_OK;
}

sdkcc_status_t
sdkcc_v1_test_transport_expect(sdkcc_test_transport_t *transport,
                               const sdkcc_test_expectation_t *expectation,
                               sdkcc_error_t *error) {
  if (transport == NULL || expectation == NULL ||
      expectation->url.data == NULL || expectation->url.len == 0U ||
      (expectation->headers == NULL && expectation->header_count != 0U)) {
    return sdkcc_internal_error_set(error, SDKCC_ERR_INVALID_ARGUMENT,
                                    SDKCC_ERROR_ARGUMENT,
                                    "invalid test expectation");
  }
  size_t required = 0U;
  if (!sdkcc_internal_checked_add(transport->expectation_count, 1U,
                                  &required)) {
    return sdkcc_internal_error_set(error, SDKCC_ERR_OVERFLOW,
                                    SDKCC_ERROR_MEMORY,
                                    "expectation count overflow");
  }
  sdkcc_status_t status = reserve_expectations(transport, required, error);
  if (status != SDKCC_OK) {
    return status;
  }
  sdkcc_owned_expectation_t *const copy =
      &transport->expectations[transport->expectation_count];
  copy->method = expectation->method;
  copy->response_status = expectation->response_status;
  status = sdkcc_internal_owned_string_copy(
      expectation->url, &transport->allocator, &copy->url, error);
  if (status != SDKCC_OK) {
    goto fail;
  }
  status = sdkcc_internal_buffer_copy(expectation->body, &transport->allocator,
                                      &copy->body, error);
  if (status != SDKCC_OK) {
    goto fail;
  }
  status = sdkcc_internal_buffer_copy(expectation->response_body,
                                      &transport->allocator,
                                      &copy->response_body, error);
  if (status != SDKCC_OK) {
    goto fail;
  }
  if (expectation->header_count != 0U) {
    size_t header_bytes = 0U;
    if (!sdkcc_internal_checked_mul(expectation->header_count,
                                    sizeof(*copy->headers), &header_bytes)) {
      status = sdkcc_internal_error_set(error, SDKCC_ERR_OVERFLOW,
                                        SDKCC_ERROR_MEMORY,
                                        "expectation header size overflow");
      goto fail;
    }
    copy->headers =
        transport->allocator.alloc(transport->allocator.ctx, header_bytes,
                                   alignof(sdkcc_owned_test_header_t));
    if (copy->headers == NULL) {
      status =
          sdkcc_internal_error_set(error, SDKCC_ERR_OOM, SDKCC_ERROR_MEMORY,
                                   "expectation header allocation failed");
      goto fail;
    }
    memset(copy->headers, 0, header_bytes);
    copy->header_count = expectation->header_count;
    for (size_t index = 0U; index < copy->header_count; ++index) {
      status = sdkcc_internal_owned_string_copy(
          expectation->headers[index].name, &transport->allocator,
          &copy->headers[index].name, error);
      if (status != SDKCC_OK) {
        goto fail;
      }
      status = sdkcc_internal_owned_string_copy(
          expectation->headers[index].value, &transport->allocator,
          &copy->headers[index].value, error);
      if (status != SDKCC_OK) {
        goto fail;
      }
    }
  }
  transport->expectation_count = required;
  return SDKCC_OK;

fail:
  expectation_reset(copy, &transport->allocator);
  return status;
}

sdkcc_status_t
sdkcc_v1_test_transport_verify(const sdkcc_test_transport_t *transport,
                               sdkcc_error_t *error) {
  if (transport == NULL) {
    return sdkcc_internal_error_set(error, SDKCC_ERR_INVALID_ARGUMENT,
                                    SDKCC_ERROR_ARGUMENT,
                                    "test transport is null");
  }
  if (transport->next_expectation != transport->expectation_count) {
    return sdkcc_internal_error_set(error, SDKCC_ERR_TEST_MISMATCH,
                                    SDKCC_ERROR_TEST,
                                    "not all expected requests were submitted");
  }
  return SDKCC_OK;
}
