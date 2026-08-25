#include "internal.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

sdkcc_string_view_t sdkcc_v1_string_view(const char *text) {
  if (text == NULL) {
    return (sdkcc_string_view_t){0};
  }
  return (sdkcc_string_view_t){.data = text, .len = strlen(text)};
}

sdkcc_string_view_t
sdkcc_v1_owned_string_view(const sdkcc_owned_string_t *string) {
  if (string == NULL) {
    return (sdkcc_string_view_t){0};
  }
  return (sdkcc_string_view_t){.data = string->data, .len = string->len};
}

sdkcc_buffer_view_t sdkcc_v1_buffer_view(const sdkcc_buffer_t *buffer) {
  if (buffer == NULL) {
    return (sdkcc_buffer_view_t){0};
  }
  return (sdkcc_buffer_view_t){.data = buffer->data, .len = buffer->len};
}

void sdkcc_v1_owned_string_reset(sdkcc_owned_string_t *string) {
  if (string == NULL) {
    return;
  }
  if (string->data != NULL && sdkcc_v1_allocator_is_valid(&string->allocator)) {
    string->allocator.free(string->allocator.ctx, string->data,
                           string->len + 1U, alignof(char));
  }
  *string = (sdkcc_owned_string_t){0};
}

void sdkcc_v1_buffer_reset(sdkcc_buffer_t *buffer) {
  if (buffer == NULL) {
    return;
  }
  if (buffer->data != NULL && sdkcc_v1_allocator_is_valid(&buffer->allocator)) {
    buffer->allocator.free(buffer->allocator.ctx, buffer->data,
                           buffer->capacity, alignof(uint8_t));
  }
  *buffer = (sdkcc_buffer_t){0};
}

void sdkcc_v1_error_reset(sdkcc_error_t *error) {
  if (error == NULL) {
    return;
  }
  sdkcc_v1_owned_string_reset(&error->provider_code);
  sdkcc_v1_owned_string_reset(&error->message);
  sdkcc_v1_owned_string_reset(&error->request_id);
  *error = (sdkcc_error_t){0};
}

sdkcc_status_t sdkcc_internal_error_set(sdkcc_error_t *error,
                                        sdkcc_status_t code,
                                        sdkcc_error_category_t category,
                                        const char *message) {
  if (error == NULL) {
    return code;
  }
  sdkcc_v1_error_reset(error);
  error->category = category;
  error->code = code;
  if (message != NULL) {
    const sdkcc_allocator_t allocator = sdkcc_v1_system_allocator();
    const sdkcc_string_view_t view = sdkcc_v1_string_view(message);
    const size_t allocation_size = view.len + 1U;
    char *const copy =
        allocator.alloc(allocator.ctx, allocation_size, alignof(char));
    if (copy != NULL) {
      memcpy(copy, view.data, view.len);
      copy[view.len] = '\0';
      error->message = (sdkcc_owned_string_t){
          .data = copy, .len = view.len, .allocator = allocator};
    }
  }
  return code;
}

sdkcc_status_t sdkcc_v1_error_set(sdkcc_error_t *error, sdkcc_status_t code,
                                  sdkcc_error_category_t category,
                                  const char *message) {
  return sdkcc_internal_error_set(error, code, category, message);
}

sdkcc_status_t sdkcc_v1_buffer_init(sdkcc_buffer_t *buffer,
                                    const sdkcc_allocator_t *allocator) {
  if (buffer == NULL) {
    return SDKCC_ERR_INVALID_ARGUMENT;
  }
  const sdkcc_allocator_t selected =
      sdkcc_internal_allocator_or_system(allocator);
  if (!sdkcc_v1_allocator_is_valid(&selected)) {
    return SDKCC_ERR_INVALID_ARGUMENT;
  }
  *buffer = (sdkcc_buffer_t){.allocator = selected};
  return SDKCC_OK;
}

sdkcc_status_t sdkcc_v1_buffer_reserve(sdkcc_buffer_t *buffer, size_t capacity,
                                       sdkcc_error_t *error) {
  if (buffer == NULL || !sdkcc_v1_allocator_is_valid(&buffer->allocator)) {
    return sdkcc_internal_error_set(error, SDKCC_ERR_INVALID_ARGUMENT,
                                    SDKCC_ERROR_ARGUMENT,
                                    "buffer is not initialized");
  }
  if (capacity <= buffer->capacity) {
    return SDKCC_OK;
  }

  size_t next = buffer->capacity == 0U ? 64U : buffer->capacity;
  while (next < capacity) {
    if (next > SIZE_MAX / 2U) {
      next = capacity;
      break;
    }
    next *= 2U;
  }
  void *const data =
      buffer->allocator.realloc(buffer->allocator.ctx, buffer->data,
                                buffer->capacity, next, alignof(uint8_t));
  if (data == NULL) {
    return sdkcc_internal_error_set(error, SDKCC_ERR_OOM, SDKCC_ERROR_MEMORY,
                                    "buffer allocation failed");
  }
  buffer->data = data;
  buffer->capacity = next;
  return SDKCC_OK;
}

sdkcc_status_t sdkcc_v1_buffer_append(sdkcc_buffer_t *buffer,
                                      sdkcc_buffer_view_t value,
                                      sdkcc_error_t *error) {
  if (buffer == NULL || (value.data == NULL && value.len != 0U)) {
    return sdkcc_internal_error_set(error, SDKCC_ERR_INVALID_ARGUMENT,
                                    SDKCC_ERROR_ARGUMENT,
                                    "invalid buffer append");
  }
  size_t required = 0U;
  if (!sdkcc_internal_checked_add(buffer->len, value.len, &required)) {
    return sdkcc_internal_error_set(error, SDKCC_ERR_OVERFLOW,
                                    SDKCC_ERROR_MEMORY, "buffer size overflow");
  }
  sdkcc_status_t status = sdkcc_v1_buffer_reserve(buffer, required, error);
  if (status != SDKCC_OK) {
    return status;
  }
  if (value.len != 0U) {
    memcpy(buffer->data + buffer->len, value.data, value.len);
  }
  buffer->len = required;
  return SDKCC_OK;
}

sdkcc_status_t sdkcc_v1_buffer_append_string(sdkcc_buffer_t *buffer,
                                             sdkcc_string_view_t value,
                                             sdkcc_error_t *error) {
  return sdkcc_v1_buffer_append(
      buffer,
      (sdkcc_buffer_view_t){.data = (const uint8_t *)value.data,
                            .len = value.len},
      error);
}

sdkcc_status_t sdkcc_v1_buffer_append_byte(sdkcc_buffer_t *buffer,
                                           uint8_t value,
                                           sdkcc_error_t *error) {
  return sdkcc_v1_buffer_append(
      buffer, (sdkcc_buffer_view_t){.data = &value, .len = 1U}, error);
}

sdkcc_status_t sdkcc_internal_owned_string_copy(
    sdkcc_string_view_t source, const sdkcc_allocator_t *allocator,
    sdkcc_owned_string_t *out, sdkcc_error_t *error) {
  if (out == NULL || (source.data == NULL && source.len != 0U)) {
    return sdkcc_internal_error_set(error, SDKCC_ERR_INVALID_ARGUMENT,
                                    SDKCC_ERROR_ARGUMENT,
                                    "invalid string copy");
  }
  const sdkcc_allocator_t selected =
      sdkcc_internal_allocator_or_system(allocator);
  if (!sdkcc_v1_allocator_is_valid(&selected) || source.len == SIZE_MAX) {
    return sdkcc_internal_error_set(error, SDKCC_ERR_INVALID_ARGUMENT,
                                    SDKCC_ERROR_ARGUMENT,
                                    "invalid string allocator or size");
  }
  sdkcc_v1_owned_string_reset(out);
  char *const copy =
      selected.alloc(selected.ctx, source.len + 1U, alignof(char));
  if (copy == NULL) {
    return sdkcc_internal_error_set(error, SDKCC_ERR_OOM, SDKCC_ERROR_MEMORY,
                                    "string allocation failed");
  }
  if (source.len != 0U) {
    memcpy(copy, source.data, source.len);
  }
  copy[source.len] = '\0';
  *out = (sdkcc_owned_string_t){
      .data = copy, .len = source.len, .allocator = selected};
  return SDKCC_OK;
}

sdkcc_status_t sdkcc_v1_owned_string_copy(sdkcc_string_view_t source,
                                          const sdkcc_allocator_t *allocator,
                                          sdkcc_owned_string_t *out,
                                          sdkcc_error_t *error) {
  return sdkcc_internal_owned_string_copy(source, allocator, out, error);
}

sdkcc_status_t sdkcc_internal_buffer_copy(sdkcc_buffer_view_t source,
                                          const sdkcc_allocator_t *allocator,
                                          sdkcc_buffer_t *out,
                                          sdkcc_error_t *error) {
  if (out == NULL || (source.data == NULL && source.len != 0U)) {
    return sdkcc_internal_error_set(error, SDKCC_ERR_INVALID_ARGUMENT,
                                    SDKCC_ERROR_ARGUMENT,
                                    "invalid buffer copy");
  }
  sdkcc_v1_buffer_reset(out);
  sdkcc_status_t status = sdkcc_v1_buffer_init(out, allocator);
  if (status != SDKCC_OK) {
    return sdkcc_internal_error_set(error, status, SDKCC_ERROR_ARGUMENT,
                                    "invalid buffer allocator");
  }
  status = sdkcc_v1_buffer_append(out, source, error);
  if (status != SDKCC_OK) {
    sdkcc_v1_buffer_reset(out);
  }
  return status;
}

sdkcc_status_t sdkcc_v1_uri_encode_append(sdkcc_buffer_t *buffer,
                                          sdkcc_string_view_t value,
                                          sdkcc_error_t *error) {
  static const char hex[] = "0123456789ABCDEF";
  if (value.data == NULL && value.len != 0U) {
    return sdkcc_internal_error_set(error, SDKCC_ERR_INVALID_ARGUMENT,
                                    SDKCC_ERROR_ARGUMENT, "invalid URI input");
  }
  for (size_t index = 0U; index < value.len; ++index) {
    const uint8_t byte = (uint8_t)value.data[index];
    const bool unreserved = (byte >= (uint8_t)'a' && byte <= (uint8_t)'z') ||
                            (byte >= (uint8_t)'A' && byte <= (uint8_t)'Z') ||
                            (byte >= (uint8_t)'0' && byte <= (uint8_t)'9') ||
                            byte == (uint8_t)'-' || byte == (uint8_t)'.' ||
                            byte == (uint8_t)'_' || byte == (uint8_t)'~';
    if (unreserved) {
      const sdkcc_status_t status =
          sdkcc_v1_buffer_append_byte(buffer, byte, error);
      if (status != SDKCC_OK) {
        return status;
      }
      continue;
    }
    const uint8_t encoded[3] = {(uint8_t)'%', (uint8_t)hex[byte >> 4U],
                                (uint8_t)hex[byte & UINT8_C(0x0f)]};
    const sdkcc_status_t status = sdkcc_v1_buffer_append(
        buffer, (sdkcc_buffer_view_t){.data = encoded, .len = 3U}, error);
    if (status != SDKCC_OK) {
      return status;
    }
  }
  return SDKCC_OK;
}
