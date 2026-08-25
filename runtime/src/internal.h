#ifndef SDKCC_RUNTIME_INTERNAL_H
#define SDKCC_RUNTIME_INTERNAL_H

#include <sdkcc/sdkcc.h>

#include <stdalign.h>

bool sdkcc_internal_checked_add(size_t left, size_t right, size_t *out);
bool sdkcc_internal_checked_mul(size_t left, size_t right, size_t *out);
sdkcc_allocator_t
sdkcc_internal_allocator_or_system(const sdkcc_allocator_t *allocator);
sdkcc_status_t sdkcc_internal_owned_string_copy(
    sdkcc_string_view_t source, const sdkcc_allocator_t *allocator,
    sdkcc_owned_string_t *out, sdkcc_error_t *error);
sdkcc_status_t sdkcc_internal_buffer_copy(sdkcc_buffer_view_t source,
                                          const sdkcc_allocator_t *allocator,
                                          sdkcc_buffer_t *out,
                                          sdkcc_error_t *error);
sdkcc_status_t sdkcc_internal_error_set(sdkcc_error_t *error,
                                        sdkcc_status_t code,
                                        sdkcc_error_category_t category,
                                        const char *message);

#endif
