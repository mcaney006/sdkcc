#include "internal.h"

#include <string.h>

typedef struct sdkcc_owned_header {
  sdkcc_owned_string_t name;
  sdkcc_owned_string_t value;
} sdkcc_owned_header_t;

struct sdkcc_request {
  sdkcc_allocator_t allocator;
  sdkcc_http_method_t method;
  sdkcc_owned_string_t url;
  sdkcc_buffer_t body;
  sdkcc_owned_header_t *headers;
  size_t header_count;
  size_t header_capacity;
};

static bool is_header_name_byte(uint8_t byte) {
  return (byte >= (uint8_t)'a' && byte <= (uint8_t)'z') ||
         (byte >= (uint8_t)'A' && byte <= (uint8_t)'Z') ||
         (byte >= (uint8_t)'0' && byte <= (uint8_t)'9') ||
         byte == (uint8_t)'!' || byte == (uint8_t)'#' || byte == (uint8_t)'$' ||
         byte == (uint8_t)'%' || byte == (uint8_t)'&' ||
         byte == (uint8_t)'\'' || byte == (uint8_t)'*' ||
         byte == (uint8_t)'+' || byte == (uint8_t)'-' || byte == (uint8_t)'.' ||
         byte == (uint8_t)'^' || byte == (uint8_t)'_' || byte == (uint8_t)'`' ||
         byte == (uint8_t)'|' || byte == (uint8_t)'~';
}

static bool header_is_valid(sdkcc_string_view_t name,
                            sdkcc_string_view_t value) {
  if (name.data == NULL || name.len == 0U ||
      (value.data == NULL && value.len != 0U)) {
    return false;
  }
  for (size_t index = 0U; index < name.len; ++index) {
    if (!is_header_name_byte((uint8_t)name.data[index])) {
      return false;
    }
  }
  for (size_t index = 0U; index < value.len; ++index) {
    const uint8_t byte = (uint8_t)value.data[index];
    if (byte == (uint8_t)'\r' || byte == (uint8_t)'\n' || byte == 0U) {
      return false;
    }
  }
  return true;
}

static void owned_header_reset(sdkcc_owned_header_t *header) {
  sdkcc_v1_owned_string_reset(&header->name);
  sdkcc_v1_owned_string_reset(&header->value);
}

sdkcc_status_t sdkcc_v1_request_create(const sdkcc_allocator_t *allocator,
                                       sdkcc_request_t **out_request,
                                       sdkcc_error_t *error) {
  if (out_request == NULL) {
    return sdkcc_internal_error_set(error, SDKCC_ERR_INVALID_ARGUMENT,
                                    SDKCC_ERROR_ARGUMENT,
                                    "request output is null");
  }
  *out_request = NULL;
  const sdkcc_allocator_t selected =
      sdkcc_internal_allocator_or_system(allocator);
  if (!sdkcc_v1_allocator_is_valid(&selected)) {
    return sdkcc_internal_error_set(error, SDKCC_ERR_INVALID_ARGUMENT,
                                    SDKCC_ERROR_ARGUMENT,
                                    "request allocator is invalid");
  }
  sdkcc_request_t *const request =
      selected.alloc(selected.ctx, sizeof(*request), alignof(sdkcc_request_t));
  if (request == NULL) {
    return sdkcc_internal_error_set(error, SDKCC_ERR_OOM, SDKCC_ERROR_MEMORY,
                                    "request allocation failed");
  }
  *request = (sdkcc_request_t){.allocator = selected};
  const sdkcc_status_t status = sdkcc_v1_buffer_init(&request->body, &selected);
  if (status != SDKCC_OK) {
    selected.free(selected.ctx, request, sizeof(*request),
                  alignof(sdkcc_request_t));
    return sdkcc_internal_error_set(error, status, SDKCC_ERROR_MEMORY,
                                    "request body initialization failed");
  }
  *out_request = request;
  return SDKCC_OK;
}

void sdkcc_v1_request_destroy(sdkcc_request_t *request) {
  if (request == NULL) {
    return;
  }
  const sdkcc_allocator_t allocator = request->allocator;
  sdkcc_v1_owned_string_reset(&request->url);
  sdkcc_v1_buffer_reset(&request->body);
  for (size_t index = 0U; index < request->header_count; ++index) {
    owned_header_reset(&request->headers[index]);
  }
  if (request->headers != NULL) {
    size_t bytes = 0U;
    if (sdkcc_internal_checked_mul(request->header_capacity,
                                   sizeof(*request->headers), &bytes)) {
      allocator.free(allocator.ctx, request->headers, bytes,
                     alignof(sdkcc_owned_header_t));
    }
  }
  allocator.free(allocator.ctx, request, sizeof(*request),
                 alignof(sdkcc_request_t));
}

sdkcc_status_t sdkcc_v1_request_set_method(sdkcc_request_t *request,
                                           sdkcc_http_method_t method,
                                           sdkcc_error_t *error) {
  if (request == NULL || method < SDKCC_HTTP_GET || method > SDKCC_HTTP_HEAD) {
    return sdkcc_internal_error_set(error, SDKCC_ERR_INVALID_ARGUMENT,
                                    SDKCC_ERROR_ARGUMENT,
                                    "invalid HTTP method");
  }
  request->method = method;
  return SDKCC_OK;
}

sdkcc_status_t sdkcc_v1_request_set_url(sdkcc_request_t *request,
                                        sdkcc_string_view_t url,
                                        sdkcc_error_t *error) {
  if (request == NULL || url.data == NULL || url.len == 0U) {
    return sdkcc_internal_error_set(error, SDKCC_ERR_INVALID_ARGUMENT,
                                    SDKCC_ERROR_ARGUMENT,
                                    "request URL is empty");
  }
  return sdkcc_internal_owned_string_copy(url, &request->allocator,
                                          &request->url, error);
}

static sdkcc_status_t reserve_headers(sdkcc_request_t *request, size_t required,
                                      sdkcc_error_t *error) {
  if (required <= request->header_capacity) {
    return SDKCC_OK;
  }
  size_t next = request->header_capacity == 0U ? 4U : request->header_capacity;
  while (next < required) {
    if (next > SIZE_MAX / 2U) {
      next = required;
      break;
    }
    next *= 2U;
  }
  size_t old_bytes = 0U;
  size_t new_bytes = 0U;
  if (!sdkcc_internal_checked_mul(request->header_capacity,
                                  sizeof(*request->headers), &old_bytes) ||
      !sdkcc_internal_checked_mul(next, sizeof(*request->headers),
                                  &new_bytes)) {
    return sdkcc_internal_error_set(error, SDKCC_ERR_OVERFLOW,
                                    SDKCC_ERROR_MEMORY,
                                    "header array size overflow");
  }
  void *const memory = request->allocator.realloc(
      request->allocator.ctx, request->headers, old_bytes, new_bytes,
      alignof(sdkcc_owned_header_t));
  if (memory == NULL) {
    return sdkcc_internal_error_set(error, SDKCC_ERR_OOM, SDKCC_ERROR_MEMORY,
                                    "header array allocation failed");
  }
  request->headers = memory;
  for (size_t index = request->header_capacity; index < next; ++index) {
    request->headers[index] = (sdkcc_owned_header_t){0};
  }
  request->header_capacity = next;
  return SDKCC_OK;
}

sdkcc_status_t sdkcc_v1_request_add_header(sdkcc_request_t *request,
                                           sdkcc_string_view_t name,
                                           sdkcc_string_view_t value,
                                           sdkcc_error_t *error) {
  if (request == NULL || !header_is_valid(name, value)) {
    return sdkcc_internal_error_set(error, SDKCC_ERR_INVALID_ARGUMENT,
                                    SDKCC_ERROR_ARGUMENT,
                                    "invalid HTTP header");
  }
  size_t required = 0U;
  if (!sdkcc_internal_checked_add(request->header_count, 1U, &required)) {
    return sdkcc_internal_error_set(
        error, SDKCC_ERR_OVERFLOW, SDKCC_ERROR_MEMORY, "header count overflow");
  }
  sdkcc_status_t status = reserve_headers(request, required, error);
  if (status != SDKCC_OK) {
    return status;
  }
  sdkcc_owned_header_t *const header = &request->headers[request->header_count];
  status = sdkcc_internal_owned_string_copy(name, &request->allocator,
                                            &header->name, error);
  if (status != SDKCC_OK) {
    return status;
  }
  status = sdkcc_internal_owned_string_copy(value, &request->allocator,
                                            &header->value, error);
  if (status != SDKCC_OK) {
    owned_header_reset(header);
    return status;
  }
  request->header_count = required;
  return SDKCC_OK;
}

sdkcc_status_t sdkcc_v1_request_set_body(sdkcc_request_t *request,
                                         sdkcc_buffer_view_t body,
                                         sdkcc_error_t *error) {
  if (request == NULL) {
    return sdkcc_internal_error_set(error, SDKCC_ERR_INVALID_ARGUMENT,
                                    SDKCC_ERROR_ARGUMENT, "request is null");
  }
  return sdkcc_internal_buffer_copy(body, &request->allocator, &request->body,
                                    error);
}

sdkcc_http_method_t sdkcc_v1_request_method(const sdkcc_request_t *request) {
  return request == NULL ? SDKCC_HTTP_INVALID : request->method;
}

sdkcc_string_view_t sdkcc_v1_request_url(const sdkcc_request_t *request) {
  return request == NULL ? (sdkcc_string_view_t){0}
                         : sdkcc_v1_owned_string_view(&request->url);
}

sdkcc_buffer_view_t sdkcc_v1_request_body(const sdkcc_request_t *request) {
  return request == NULL ? (sdkcc_buffer_view_t){0}
                         : sdkcc_v1_buffer_view(&request->body);
}

size_t sdkcc_v1_request_header_count(const sdkcc_request_t *request) {
  return request == NULL ? 0U : request->header_count;
}

sdkcc_status_t sdkcc_v1_request_header_at(const sdkcc_request_t *request,
                                          size_t index,
                                          sdkcc_header_view_t *out_header) {
  if (request == NULL || out_header == NULL || index >= request->header_count) {
    return SDKCC_ERR_INVALID_ARGUMENT;
  }
  *out_header = (sdkcc_header_view_t){
      .name = sdkcc_v1_owned_string_view(&request->headers[index].name),
      .value = sdkcc_v1_owned_string_view(&request->headers[index].value),
  };
  return SDKCC_OK;
}

void sdkcc_v1_response_reset(sdkcc_response_t *response) {
  if (response == NULL) {
    return;
  }
  sdkcc_v1_buffer_reset(&response->body);
  *response = (sdkcc_response_t){0};
}

sdkcc_status_t
sdkcc_v1_transport_send(const sdkcc_transport_t *transport,
                        const sdkcc_request_t *request,
                        const sdkcc_allocator_t *response_allocator,
                        sdkcc_response_t *out_response, sdkcc_error_t *error) {
  if (transport == NULL || transport->vtable == NULL ||
      transport->vtable->abi_version != SDKCC_ABI_VERSION ||
      transport->vtable->struct_size < sizeof(sdkcc_transport_vtable_t) ||
      transport->vtable->send == NULL || request == NULL ||
      request->method == SDKCC_HTTP_INVALID || request->url.data == NULL ||
      out_response == NULL) {
    return sdkcc_internal_error_set(error, SDKCC_ERR_INVALID_ARGUMENT,
                                    SDKCC_ERROR_ARGUMENT,
                                    "invalid transport submission");
  }
  const sdkcc_allocator_t selected =
      sdkcc_internal_allocator_or_system(response_allocator);
  if (!sdkcc_v1_allocator_is_valid(&selected)) {
    return sdkcc_internal_error_set(error, SDKCC_ERR_INVALID_ARGUMENT,
                                    SDKCC_ERROR_ARGUMENT,
                                    "response allocator is invalid");
  }
  sdkcc_v1_response_reset(out_response);
  return transport->vtable->send(transport->ctx, request, &selected,
                                 out_response, error);
}
