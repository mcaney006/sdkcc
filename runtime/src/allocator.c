#include "internal.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct sdkcc_system_block {
  void *base;
  size_t size;
} sdkcc_system_block_t;

static bool alignment_is_valid(size_t alignment) {
  return alignment != 0U && (alignment & (alignment - 1U)) == 0U;
}

bool sdkcc_internal_checked_add(size_t left, size_t right, size_t *out) {
  if (out == NULL || left > SIZE_MAX - right) {
    return false;
  }
  *out = left + right;
  return true;
}

bool sdkcc_internal_checked_mul(size_t left, size_t right, size_t *out) {
  if (out == NULL || (left != 0U && right > SIZE_MAX / left)) {
    return false;
  }
  *out = left * right;
  return true;
}

static void *system_alloc(void *ctx, size_t size, size_t alignment) {
  (void)ctx;
  if (alignment < alignof(max_align_t)) {
    alignment = alignof(max_align_t);
  }
  if (!alignment_is_valid(alignment)) {
    return NULL;
  }

  const size_t actual_size = size == 0U ? 1U : size;
  size_t overhead = 0U;
  size_t total = 0U;
  if (!sdkcc_internal_checked_add(sizeof(sdkcc_system_block_t), alignment - 1U,
                                  &overhead) ||
      !sdkcc_internal_checked_add(actual_size, overhead, &total)) {
    return NULL;
  }

  void *const base = malloc(total);
  if (base == NULL) {
    return NULL;
  }

  const uintptr_t raw = (uintptr_t)base + sizeof(sdkcc_system_block_t);
  const uintptr_t aligned =
      (raw + (uintptr_t)(alignment - 1U)) & ~(uintptr_t)(alignment - 1U);
  sdkcc_system_block_t *const block =
      (sdkcc_system_block_t *)(aligned - sizeof(sdkcc_system_block_t));
  block->base = base;
  block->size = actual_size;
  return (void *)aligned;
}

static void system_free(void *ctx, void *ptr, size_t size, size_t alignment) {
  (void)ctx;
  (void)size;
  (void)alignment;
  if (ptr == NULL) {
    return;
  }
  sdkcc_system_block_t *const block =
      (sdkcc_system_block_t *)((uintptr_t)ptr - sizeof(sdkcc_system_block_t));
  free(block->base);
}

static void *system_realloc(void *ctx, void *ptr, size_t old_size,
                            size_t new_size, size_t alignment) {
  (void)old_size;
  if (ptr == NULL) {
    return system_alloc(ctx, new_size, alignment);
  }
  if (new_size == 0U) {
    system_free(ctx, ptr, old_size, alignment);
    return NULL;
  }

  const sdkcc_system_block_t *const old_block =
      (const sdkcc_system_block_t *)((uintptr_t)ptr -
                                     sizeof(sdkcc_system_block_t));
  void *const replacement = system_alloc(ctx, new_size, alignment);
  if (replacement == NULL) {
    return NULL;
  }
  const size_t copy_size =
      old_block->size < new_size ? old_block->size : new_size;
  memcpy(replacement, ptr, copy_size);
  system_free(ctx, ptr, old_size, alignment);
  return replacement;
}

sdkcc_allocator_t sdkcc_v1_system_allocator(void) {
  return (sdkcc_allocator_t){
      .ctx = NULL,
      .alloc = system_alloc,
      .realloc = system_realloc,
      .free = system_free,
  };
}

bool sdkcc_v1_allocator_is_valid(const sdkcc_allocator_t *allocator) {
  return allocator != NULL && allocator->alloc != NULL &&
         allocator->realloc != NULL && allocator->free != NULL;
}

sdkcc_allocator_t
sdkcc_internal_allocator_or_system(const sdkcc_allocator_t *allocator) {
  if (allocator == NULL ||
      (allocator->alloc == NULL && allocator->realloc == NULL &&
       allocator->free == NULL)) {
    return sdkcc_v1_system_allocator();
  }
  return *allocator;
}
