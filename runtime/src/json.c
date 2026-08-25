#include "internal.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

typedef struct sdkcc_json_scanner {
  const uint8_t *cursor;
  const uint8_t *end;
} sdkcc_json_scanner_t;

static void skip_whitespace(sdkcc_json_scanner_t *scanner) {
  while (scanner->cursor < scanner->end) {
    const uint8_t byte = *scanner->cursor;
    if (byte != (uint8_t)' ' && byte != (uint8_t)'\t' &&
        byte != (uint8_t)'\r' && byte != (uint8_t)'\n') {
      break;
    }
    ++scanner->cursor;
  }
}

static int hex_value(uint8_t byte) {
  if (byte >= (uint8_t)'0' && byte <= (uint8_t)'9') {
    return (int)(byte - (uint8_t)'0');
  }
  if (byte >= (uint8_t)'a' && byte <= (uint8_t)'f') {
    return 10 + (int)(byte - (uint8_t)'a');
  }
  if (byte >= (uint8_t)'A' && byte <= (uint8_t)'F') {
    return 10 + (int)(byte - (uint8_t)'A');
  }
  return -1;
}

static bool parse_hex4(const uint8_t *input, const uint8_t *end,
                       uint32_t *out) {
  if ((size_t)(end - input) < 4U) {
    return false;
  }
  uint32_t value = 0U;
  for (size_t index = 0U; index < 4U; ++index) {
    const int digit = hex_value(input[index]);
    if (digit < 0) {
      return false;
    }
    value = (value << 4U) | (uint32_t)digit;
  }
  *out = value;
  return true;
}

static size_t valid_utf8_sequence(const uint8_t *input, const uint8_t *end) {
  if (input >= end) {
    return 0U;
  }
  const uint8_t first = input[0];
  if (first < UINT8_C(0x80)) {
    return 1U;
  }
  if (first >= UINT8_C(0xc2) && first <= UINT8_C(0xdf)) {
    return (size_t)(end - input) >= 2U && input[1] >= UINT8_C(0x80) &&
                   input[1] <= UINT8_C(0xbf)
               ? 2U
               : 0U;
  }
  if ((size_t)(end - input) >= 3U) {
    const uint8_t second = input[1];
    const uint8_t third = input[2];
    const bool continuation = third >= UINT8_C(0x80) && third <= UINT8_C(0xbf);
    if (first == UINT8_C(0xe0) && second >= UINT8_C(0xa0) &&
        second <= UINT8_C(0xbf) && continuation) {
      return 3U;
    }
    if (((first >= UINT8_C(0xe1) && first <= UINT8_C(0xec)) ||
         (first >= UINT8_C(0xee) && first <= UINT8_C(0xef))) &&
        second >= UINT8_C(0x80) && second <= UINT8_C(0xbf) && continuation) {
      return 3U;
    }
    if (first == UINT8_C(0xed) && second >= UINT8_C(0x80) &&
        second <= UINT8_C(0x9f) && continuation) {
      return 3U;
    }
  }
  if ((size_t)(end - input) >= 4U) {
    const uint8_t second = input[1];
    const bool tails = input[2] >= UINT8_C(0x80) && input[2] <= UINT8_C(0xbf) &&
                       input[3] >= UINT8_C(0x80) && input[3] <= UINT8_C(0xbf);
    if (first == UINT8_C(0xf0) && second >= UINT8_C(0x90) &&
        second <= UINT8_C(0xbf) && tails) {
      return 4U;
    }
    if (first >= UINT8_C(0xf1) && first <= UINT8_C(0xf3) &&
        second >= UINT8_C(0x80) && second <= UINT8_C(0xbf) && tails) {
      return 4U;
    }
    if (first == UINT8_C(0xf4) && second >= UINT8_C(0x80) &&
        second <= UINT8_C(0x8f) && tails) {
      return 4U;
    }
  }
  return 0U;
}

static sdkcc_status_t parse_error(sdkcc_error_t *error, const char *message) {
  return sdkcc_internal_error_set(error, SDKCC_ERR_PARSE,
                                  SDKCC_ERROR_SERIALIZATION, message);
}

static sdkcc_status_t scan_string(sdkcc_json_scanner_t *scanner,
                                  sdkcc_string_view_t *out_raw,
                                  sdkcc_error_t *error) {
  if (scanner->cursor >= scanner->end || *scanner->cursor != (uint8_t)'"') {
    return parse_error(error, "expected JSON string");
  }
  ++scanner->cursor;
  const uint8_t *const start = scanner->cursor;
  while (scanner->cursor < scanner->end) {
    const uint8_t byte = *scanner->cursor;
    if (byte == (uint8_t)'"') {
      if (out_raw != NULL) {
        *out_raw =
            (sdkcc_string_view_t){.data = (const char *)start,
                                  .len = (size_t)(scanner->cursor - start)};
      }
      ++scanner->cursor;
      return SDKCC_OK;
    }
    if (byte == (uint8_t)'\\') {
      ++scanner->cursor;
      if (scanner->cursor >= scanner->end) {
        return parse_error(error, "truncated JSON escape");
      }
      const uint8_t escaped = *scanner->cursor++;
      if (escaped == (uint8_t)'"' || escaped == (uint8_t)'\\' ||
          escaped == (uint8_t)'/' || escaped == (uint8_t)'b' ||
          escaped == (uint8_t)'f' || escaped == (uint8_t)'n' ||
          escaped == (uint8_t)'r' || escaped == (uint8_t)'t') {
        continue;
      }
      if (escaped != (uint8_t)'u') {
        return parse_error(error, "invalid JSON escape");
      }
      uint32_t code_unit = 0U;
      if (!parse_hex4(scanner->cursor, scanner->end, &code_unit)) {
        return parse_error(error, "invalid JSON Unicode escape");
      }
      scanner->cursor += 4;
      if (code_unit >= UINT32_C(0xd800) && code_unit <= UINT32_C(0xdbff)) {
        if ((size_t)(scanner->end - scanner->cursor) < 6U ||
            scanner->cursor[0] != (uint8_t)'\\' ||
            scanner->cursor[1] != (uint8_t)'u') {
          return parse_error(error, "unpaired JSON high surrogate");
        }
        uint32_t low = 0U;
        if (!parse_hex4(scanner->cursor + 2, scanner->end, &low) ||
            low < UINT32_C(0xdc00) || low > UINT32_C(0xdfff)) {
          return parse_error(error, "invalid JSON surrogate pair");
        }
        scanner->cursor += 6;
      } else if (code_unit >= UINT32_C(0xdc00) &&
                 code_unit <= UINT32_C(0xdfff)) {
        return parse_error(error, "unpaired JSON low surrogate");
      }
      continue;
    }
    if (byte < UINT8_C(0x20)) {
      return parse_error(error, "unescaped control byte in JSON string");
    }
    const size_t width = valid_utf8_sequence(scanner->cursor, scanner->end);
    if (width == 0U) {
      return parse_error(error, "invalid UTF-8 in JSON string");
    }
    scanner->cursor += width;
  }
  return parse_error(error, "unterminated JSON string");
}

static sdkcc_status_t scan_number(sdkcc_json_scanner_t *scanner,
                                  sdkcc_error_t *error) {
  const uint8_t *cursor = scanner->cursor;
  if (cursor < scanner->end && *cursor == (uint8_t)'-') {
    ++cursor;
  }
  if (cursor >= scanner->end) {
    return parse_error(error, "truncated JSON number");
  }
  if (*cursor == (uint8_t)'0') {
    ++cursor;
    if (cursor < scanner->end && *cursor >= (uint8_t)'0' &&
        *cursor <= (uint8_t)'9') {
      return parse_error(error, "leading zero in JSON number");
    }
  } else if (*cursor >= (uint8_t)'1' && *cursor <= (uint8_t)'9') {
    do {
      ++cursor;
    } while (cursor < scanner->end && *cursor >= (uint8_t)'0' &&
             *cursor <= (uint8_t)'9');
  } else {
    return parse_error(error, "invalid JSON number");
  }
  if (cursor < scanner->end && *cursor == (uint8_t)'.') {
    ++cursor;
    if (cursor >= scanner->end || *cursor < (uint8_t)'0' ||
        *cursor > (uint8_t)'9') {
      return parse_error(error, "invalid JSON fraction");
    }
    do {
      ++cursor;
    } while (cursor < scanner->end && *cursor >= (uint8_t)'0' &&
             *cursor <= (uint8_t)'9');
  }
  if (cursor < scanner->end &&
      (*cursor == (uint8_t)'e' || *cursor == (uint8_t)'E')) {
    ++cursor;
    if (cursor < scanner->end &&
        (*cursor == (uint8_t)'+' || *cursor == (uint8_t)'-')) {
      ++cursor;
    }
    if (cursor >= scanner->end || *cursor < (uint8_t)'0' ||
        *cursor > (uint8_t)'9') {
      return parse_error(error, "invalid JSON exponent");
    }
    do {
      ++cursor;
    } while (cursor < scanner->end && *cursor >= (uint8_t)'0' &&
             *cursor <= (uint8_t)'9');
  }
  scanner->cursor = cursor;
  return SDKCC_OK;
}

static sdkcc_status_t scan_value(sdkcc_json_scanner_t *scanner, uint32_t depth,
                                 sdkcc_json_kind_t *out_kind,
                                 sdkcc_error_t *error);

static sdkcc_status_t scan_array(sdkcc_json_scanner_t *scanner, uint32_t depth,
                                 sdkcc_error_t *error) {
  ++scanner->cursor;
  skip_whitespace(scanner);
  if (scanner->cursor < scanner->end && *scanner->cursor == (uint8_t)']') {
    ++scanner->cursor;
    return SDKCC_OK;
  }
  for (;;) {
    sdkcc_status_t status = scan_value(scanner, depth, NULL, error);
    if (status != SDKCC_OK) {
      return status;
    }
    skip_whitespace(scanner);
    if (scanner->cursor >= scanner->end) {
      return parse_error(error, "unterminated JSON array");
    }
    if (*scanner->cursor == (uint8_t)']') {
      ++scanner->cursor;
      return SDKCC_OK;
    }
    if (*scanner->cursor++ != (uint8_t)',') {
      return parse_error(error, "expected comma in JSON array");
    }
    skip_whitespace(scanner);
  }
}

static sdkcc_status_t scan_object(sdkcc_json_scanner_t *scanner, uint32_t depth,
                                  sdkcc_error_t *error) {
  ++scanner->cursor;
  skip_whitespace(scanner);
  if (scanner->cursor < scanner->end && *scanner->cursor == (uint8_t)'}') {
    ++scanner->cursor;
    return SDKCC_OK;
  }
  for (;;) {
    sdkcc_status_t status = scan_string(scanner, NULL, error);
    if (status != SDKCC_OK) {
      return status;
    }
    skip_whitespace(scanner);
    if (scanner->cursor >= scanner->end || *scanner->cursor != (uint8_t)':') {
      return parse_error(error, "expected colon in JSON object");
    }
    ++scanner->cursor;
    skip_whitespace(scanner);
    status = scan_value(scanner, depth, NULL, error);
    if (status != SDKCC_OK) {
      return status;
    }
    skip_whitespace(scanner);
    if (scanner->cursor >= scanner->end) {
      return parse_error(error, "unterminated JSON object");
    }
    if (*scanner->cursor == (uint8_t)'}') {
      ++scanner->cursor;
      return SDKCC_OK;
    }
    if (*scanner->cursor++ != (uint8_t)',') {
      return parse_error(error, "expected comma in JSON object");
    }
    skip_whitespace(scanner);
  }
}

static bool consume_literal(sdkcc_json_scanner_t *scanner, const char *literal,
                            size_t length) {
  if ((size_t)(scanner->end - scanner->cursor) < length ||
      memcmp(scanner->cursor, literal, length) != 0) {
    return false;
  }
  scanner->cursor += length;
  return true;
}

static sdkcc_status_t scan_value(sdkcc_json_scanner_t *scanner, uint32_t depth,
                                 sdkcc_json_kind_t *out_kind,
                                 sdkcc_error_t *error) {
  skip_whitespace(scanner);
  if (scanner->cursor >= scanner->end) {
    return parse_error(error, "expected JSON value");
  }
  const uint8_t byte = *scanner->cursor;
  sdkcc_json_kind_t kind = SDKCC_JSON_NULL;
  sdkcc_status_t status = SDKCC_OK;
  if (byte == (uint8_t)'"') {
    kind = SDKCC_JSON_STRING;
    status = scan_string(scanner, NULL, error);
  } else if (byte == (uint8_t)'{' || byte == (uint8_t)'[') {
    if (depth >= SDKCC_JSON_MAX_DEPTH) {
      return parse_error(error, "JSON nesting limit exceeded");
    }
    if (byte == (uint8_t)'{') {
      kind = SDKCC_JSON_OBJECT;
      status = scan_object(scanner, depth + 1U, error);
    } else {
      kind = SDKCC_JSON_ARRAY;
      status = scan_array(scanner, depth + 1U, error);
    }
  } else if (byte == (uint8_t)'t') {
    kind = SDKCC_JSON_BOOLEAN;
    status = consume_literal(scanner, "true", 4U)
                 ? SDKCC_OK
                 : parse_error(error, "invalid JSON literal");
  } else if (byte == (uint8_t)'f') {
    kind = SDKCC_JSON_BOOLEAN;
    status = consume_literal(scanner, "false", 5U)
                 ? SDKCC_OK
                 : parse_error(error, "invalid JSON literal");
  } else if (byte == (uint8_t)'n') {
    kind = SDKCC_JSON_NULL;
    status = consume_literal(scanner, "null", 4U)
                 ? SDKCC_OK
                 : parse_error(error, "invalid JSON literal");
  } else if (byte == (uint8_t)'-' ||
             (byte >= (uint8_t)'0' && byte <= (uint8_t)'9')) {
    kind = SDKCC_JSON_NUMBER;
    status = scan_number(scanner, error);
  } else {
    status = parse_error(error, "invalid JSON value");
  }
  if (status == SDKCC_OK && out_kind != NULL) {
    *out_kind = kind;
  }
  return status;
}

sdkcc_status_t sdkcc_v1_json_object_visit(sdkcc_buffer_view_t json,
                                          sdkcc_json_member_fn visitor,
                                          void *visitor_ctx,
                                          sdkcc_error_t *error) {
  if (json.data == NULL || visitor == NULL) {
    return sdkcc_internal_error_set(error, SDKCC_ERR_INVALID_ARGUMENT,
                                    SDKCC_ERROR_ARGUMENT,
                                    "invalid JSON visitor input");
  }
  sdkcc_json_scanner_t scanner = {.cursor = json.data,
                                  .end = json.data + json.len};
  skip_whitespace(&scanner);
  if (scanner.cursor >= scanner.end || *scanner.cursor != (uint8_t)'{') {
    return parse_error(error, "expected top-level JSON object");
  }
  ++scanner.cursor;
  skip_whitespace(&scanner);
  if (scanner.cursor < scanner.end && *scanner.cursor == (uint8_t)'}') {
    ++scanner.cursor;
  } else {
    for (;;) {
      sdkcc_string_view_t key = {0};
      sdkcc_status_t status = scan_string(&scanner, &key, error);
      if (status != SDKCC_OK) {
        return status;
      }
      skip_whitespace(&scanner);
      if (scanner.cursor >= scanner.end || *scanner.cursor != (uint8_t)':') {
        return parse_error(error, "expected colon in JSON object");
      }
      ++scanner.cursor;
      skip_whitespace(&scanner);
      const uint8_t *const value_start = scanner.cursor;
      sdkcc_json_kind_t kind = SDKCC_JSON_NULL;
      status = scan_value(&scanner, 1U, &kind, error);
      if (status != SDKCC_OK) {
        return status;
      }
      const sdkcc_json_value_t value = {
          .kind = kind,
          .raw = {.data = value_start,
                  .len = (size_t)(scanner.cursor - value_start)}};
      status = visitor(visitor_ctx, key, value, error);
      if (status != SDKCC_OK) {
        return status;
      }
      skip_whitespace(&scanner);
      if (scanner.cursor >= scanner.end) {
        return parse_error(error, "unterminated JSON object");
      }
      if (*scanner.cursor == (uint8_t)'}') {
        ++scanner.cursor;
        break;
      }
      if (*scanner.cursor++ != (uint8_t)',') {
        return parse_error(error, "expected comma in JSON object");
      }
      skip_whitespace(&scanner);
    }
  }
  skip_whitespace(&scanner);
  if (scanner.cursor != scanner.end) {
    return parse_error(error, "trailing data after JSON object");
  }
  return SDKCC_OK;
}

static size_t encode_codepoint(uint32_t codepoint, uint8_t output[4]) {
  if (codepoint <= UINT32_C(0x7f)) {
    output[0] = (uint8_t)codepoint;
    return 1U;
  }
  if (codepoint <= UINT32_C(0x7ff)) {
    output[0] = (uint8_t)(UINT32_C(0xc0) | (codepoint >> 6U));
    output[1] = (uint8_t)(UINT32_C(0x80) | (codepoint & UINT32_C(0x3f)));
    return 2U;
  }
  if (codepoint <= UINT32_C(0xffff)) {
    output[0] = (uint8_t)(UINT32_C(0xe0) | (codepoint >> 12U));
    output[1] =
        (uint8_t)(UINT32_C(0x80) | ((codepoint >> 6U) & UINT32_C(0x3f)));
    output[2] = (uint8_t)(UINT32_C(0x80) | (codepoint & UINT32_C(0x3f)));
    return 3U;
  }
  output[0] = (uint8_t)(UINT32_C(0xf0) | (codepoint >> 18U));
  output[1] = (uint8_t)(UINT32_C(0x80) | ((codepoint >> 12U) & UINT32_C(0x3f)));
  output[2] = (uint8_t)(UINT32_C(0x80) | ((codepoint >> 6U) & UINT32_C(0x3f)));
  output[3] = (uint8_t)(UINT32_C(0x80) | (codepoint & UINT32_C(0x3f)));
  return 4U;
}

static sdkcc_status_t decode_content(sdkcc_string_view_t raw, uint8_t *output,
                                     size_t *out_length) {
  size_t written = 0U;
  size_t index = 0U;
  while (index < raw.len) {
    const uint8_t byte = (uint8_t)raw.data[index];
    if (byte != (uint8_t)'\\') {
      const size_t width =
          valid_utf8_sequence((const uint8_t *)raw.data + index,
                              (const uint8_t *)raw.data + raw.len);
      if (width == 0U || byte < UINT8_C(0x20)) {
        return SDKCC_ERR_PARSE;
      }
      if (output != NULL) {
        memcpy(output + written, raw.data + index, width);
      }
      written += width;
      index += width;
      continue;
    }
    ++index;
    if (index >= raw.len) {
      return SDKCC_ERR_PARSE;
    }
    const uint8_t escaped = (uint8_t)raw.data[index++];
    uint8_t decoded[4] = {0};
    size_t decoded_length = 1U;
    switch (escaped) {
    case (uint8_t)'"':
    case (uint8_t)'\\':
    case (uint8_t)'/':
      decoded[0] = escaped;
      break;
    case (uint8_t)'b':
      decoded[0] = (uint8_t)'\b';
      break;
    case (uint8_t)'f':
      decoded[0] = (uint8_t)'\f';
      break;
    case (uint8_t)'n':
      decoded[0] = (uint8_t)'\n';
      break;
    case (uint8_t)'r':
      decoded[0] = (uint8_t)'\r';
      break;
    case (uint8_t)'t':
      decoded[0] = (uint8_t)'\t';
      break;
    case (uint8_t)'u': {
      uint32_t codepoint = 0U;
      if (!parse_hex4((const uint8_t *)raw.data + index,
                      (const uint8_t *)raw.data + raw.len, &codepoint)) {
        return SDKCC_ERR_PARSE;
      }
      index += 4U;
      if (codepoint >= UINT32_C(0xd800) && codepoint <= UINT32_C(0xdbff)) {
        if (raw.len - index < 6U || raw.data[index] != '\\' ||
            raw.data[index + 1U] != 'u') {
          return SDKCC_ERR_PARSE;
        }
        uint32_t low = 0U;
        if (!parse_hex4((const uint8_t *)raw.data + index + 2U,
                        (const uint8_t *)raw.data + raw.len, &low) ||
            low < UINT32_C(0xdc00) || low > UINT32_C(0xdfff)) {
          return SDKCC_ERR_PARSE;
        }
        index += 6U;
        codepoint = UINT32_C(0x10000) +
                    ((codepoint - UINT32_C(0xd800)) << 10U) +
                    (low - UINT32_C(0xdc00));
      } else if (codepoint >= UINT32_C(0xdc00) &&
                 codepoint <= UINT32_C(0xdfff)) {
        return SDKCC_ERR_PARSE;
      }
      decoded_length = encode_codepoint(codepoint, decoded);
      break;
    }
    default:
      return SDKCC_ERR_PARSE;
    }
    if (output != NULL) {
      memcpy(output + written, decoded, decoded_length);
    }
    written += decoded_length;
  }
  *out_length = written;
  return SDKCC_OK;
}

bool sdkcc_v1_json_key_equals(sdkcc_string_view_t raw_key,
                              sdkcc_string_view_t expected) {
  if ((raw_key.data == NULL && raw_key.len != 0U) ||
      (expected.data == NULL && expected.len != 0U)) {
    return false;
  }
  size_t decoded_length = 0U;
  if (decode_content(raw_key, NULL, &decoded_length) != SDKCC_OK ||
      decoded_length != expected.len) {
    return false;
  }
  if (decoded_length == 0U) {
    return true;
  }
  /* ponytail: key lengths are input-bounded; stack scratch avoids allocator
     traffic. Raise the key limit with the parser resource budget if needed. */
  if (decoded_length > 4096U) {
    return false;
  }
  uint8_t decoded[4096];
  size_t written = 0U;
  return decode_content(raw_key, decoded, &written) == SDKCC_OK &&
         written == expected.len &&
         memcmp(decoded, expected.data, expected.len) == 0;
}

sdkcc_status_t sdkcc_v1_json_value_to_owned_string(
    sdkcc_json_value_t value, const sdkcc_allocator_t *allocator,
    sdkcc_owned_string_t *out_string, sdkcc_error_t *error) {
  if (out_string == NULL || value.kind != SDKCC_JSON_STRING ||
      value.raw.data == NULL || value.raw.len < 2U) {
    return sdkcc_internal_error_set(error, SDKCC_ERR_SCHEMA,
                                    SDKCC_ERROR_SERIALIZATION,
                                    "expected JSON string value");
  }
  sdkcc_json_scanner_t scanner = {.cursor = value.raw.data,
                                  .end = value.raw.data + value.raw.len};
  sdkcc_string_view_t raw = {0};
  sdkcc_status_t status = scan_string(&scanner, &raw, error);
  if (status != SDKCC_OK || scanner.cursor != scanner.end) {
    return status == SDKCC_OK
               ? parse_error(error, "trailing data after JSON string")
               : status;
  }
  size_t decoded_length = 0U;
  status = decode_content(raw, NULL, &decoded_length);
  if (status != SDKCC_OK || decoded_length == SIZE_MAX) {
    return parse_error(error, "invalid JSON string encoding");
  }
  const sdkcc_allocator_t selected =
      sdkcc_internal_allocator_or_system(allocator);
  if (!sdkcc_v1_allocator_is_valid(&selected)) {
    return sdkcc_internal_error_set(error, SDKCC_ERR_INVALID_ARGUMENT,
                                    SDKCC_ERROR_ARGUMENT,
                                    "string allocator is invalid");
  }
  sdkcc_v1_owned_string_reset(out_string);
  char *const output =
      selected.alloc(selected.ctx, decoded_length + 1U, alignof(char));
  if (output == NULL) {
    return sdkcc_internal_error_set(error, SDKCC_ERR_OOM, SDKCC_ERROR_MEMORY,
                                    "decoded string allocation failed");
  }
  size_t written = 0U;
  status = decode_content(raw, (uint8_t *)output, &written);
  if (status != SDKCC_OK || written != decoded_length) {
    selected.free(selected.ctx, output, decoded_length + 1U, alignof(char));
    return parse_error(error, "invalid JSON string encoding");
  }
  output[written] = '\0';
  *out_string = (sdkcc_owned_string_t){
      .data = output, .len = written, .allocator = selected};
  return SDKCC_OK;
}

sdkcc_status_t sdkcc_v1_json_value_to_i64(sdkcc_json_value_t value,
                                          int64_t *out_value,
                                          sdkcc_error_t *error) {
  if (out_value == NULL || value.kind != SDKCC_JSON_NUMBER ||
      value.raw.data == NULL || value.raw.len == 0U) {
    return sdkcc_internal_error_set(error, SDKCC_ERR_SCHEMA,
                                    SDKCC_ERROR_SERIALIZATION,
                                    "expected JSON integer value");
  }
  sdkcc_json_scanner_t scanner = {.cursor = value.raw.data,
                                  .end = value.raw.data + value.raw.len};
  sdkcc_status_t status = scan_number(&scanner, error);
  if (status != SDKCC_OK || scanner.cursor != scanner.end) {
    return status == SDKCC_OK
               ? parse_error(error, "trailing data after JSON number")
               : status;
  }
  bool negative = false;
  size_t index = 0U;
  if (value.raw.data[index] == (uint8_t)'-') {
    negative = true;
    ++index;
  }
  uint64_t magnitude = 0U;
  const uint64_t limit =
      negative ? (uint64_t)INT64_MAX + UINT64_C(1) : (uint64_t)INT64_MAX;
  for (; index < value.raw.len; ++index) {
    const uint8_t byte = value.raw.data[index];
    if (byte < (uint8_t)'0' || byte > (uint8_t)'9') {
      return sdkcc_internal_error_set(error, SDKCC_ERR_SCHEMA,
                                      SDKCC_ERROR_SERIALIZATION,
                                      "expected integral JSON number");
    }
    const uint64_t digit = (uint64_t)(byte - (uint8_t)'0');
    if (magnitude > (limit - digit) / UINT64_C(10)) {
      return sdkcc_internal_error_set(error, SDKCC_ERR_OVERFLOW,
                                      SDKCC_ERROR_SERIALIZATION,
                                      "JSON integer is outside int64 range");
    }
    magnitude = magnitude * UINT64_C(10) + digit;
  }
  if (negative && magnitude == (uint64_t)INT64_MAX + UINT64_C(1)) {
    *out_value = INT64_MIN;
  } else if (negative) {
    *out_value = -(int64_t)magnitude;
  } else {
    *out_value = (int64_t)magnitude;
  }
  return SDKCC_OK;
}

sdkcc_status_t sdkcc_v1_json_value_to_bool(sdkcc_json_value_t value,
                                           bool *out_value,
                                           sdkcc_error_t *error) {
  if (out_value == NULL || value.kind != SDKCC_JSON_BOOLEAN ||
      value.raw.data == NULL) {
    return sdkcc_internal_error_set(error, SDKCC_ERR_SCHEMA,
                                    SDKCC_ERROR_SERIALIZATION,
                                    "expected JSON boolean value");
  }
  if (value.raw.len == 4U && memcmp(value.raw.data, "true", 4U) == 0) {
    *out_value = true;
    return SDKCC_OK;
  }
  if (value.raw.len == 5U && memcmp(value.raw.data, "false", 5U) == 0) {
    *out_value = false;
    return SDKCC_OK;
  }
  return parse_error(error, "invalid JSON boolean");
}

sdkcc_status_t sdkcc_v1_json_write_string(sdkcc_buffer_t *buffer,
                                          sdkcc_string_view_t value,
                                          sdkcc_error_t *error) {
  if (buffer == NULL || (value.data == NULL && value.len != 0U)) {
    return sdkcc_internal_error_set(error, SDKCC_ERR_INVALID_ARGUMENT,
                                    SDKCC_ERROR_ARGUMENT,
                                    "invalid JSON string input");
  }
  sdkcc_status_t status =
      sdkcc_v1_buffer_append_byte(buffer, (uint8_t)'"', error);
  if (status != SDKCC_OK) {
    return status;
  }
  size_t index = 0U;
  static const char hex[] = "0123456789ABCDEF";
  while (index < value.len) {
    const uint8_t byte = (uint8_t)value.data[index];
    const char *escape = NULL;
    size_t escape_length = 0U;
    switch (byte) {
    case (uint8_t)'"':
      escape = "\\\"";
      escape_length = 2U;
      break;
    case (uint8_t)'\\':
      escape = "\\\\";
      escape_length = 2U;
      break;
    case (uint8_t)'\b':
      escape = "\\b";
      escape_length = 2U;
      break;
    case (uint8_t)'\f':
      escape = "\\f";
      escape_length = 2U;
      break;
    case (uint8_t)'\n':
      escape = "\\n";
      escape_length = 2U;
      break;
    case (uint8_t)'\r':
      escape = "\\r";
      escape_length = 2U;
      break;
    case (uint8_t)'\t':
      escape = "\\t";
      escape_length = 2U;
      break;
    default:
      break;
    }
    if (escape != NULL) {
      status = sdkcc_v1_buffer_append_string(
          buffer, (sdkcc_string_view_t){.data = escape, .len = escape_length},
          error);
      ++index;
    } else if (byte < UINT8_C(0x20)) {
      const uint8_t escaped[6] = {(uint8_t)'\\',
                                  (uint8_t)'u',
                                  (uint8_t)'0',
                                  (uint8_t)'0',
                                  (uint8_t)hex[byte >> 4U],
                                  (uint8_t)hex[byte & UINT8_C(0x0f)]};
      status = sdkcc_v1_buffer_append(
          buffer, (sdkcc_buffer_view_t){.data = escaped, .len = 6U}, error);
      ++index;
    } else {
      const size_t width =
          valid_utf8_sequence((const uint8_t *)value.data + index,
                              (const uint8_t *)value.data + value.len);
      if (width == 0U) {
        return sdkcc_internal_error_set(error, SDKCC_ERR_SERIALIZE,
                                        SDKCC_ERROR_SERIALIZATION,
                                        "invalid UTF-8 in string value");
      }
      status = sdkcc_v1_buffer_append(
          buffer,
          (sdkcc_buffer_view_t){.data = (const uint8_t *)value.data + index,
                                .len = width},
          error);
      index += width;
    }
    if (status != SDKCC_OK) {
      return status;
    }
  }
  return sdkcc_v1_buffer_append_byte(buffer, (uint8_t)'"', error);
}

sdkcc_status_t sdkcc_v1_json_write_i64(sdkcc_buffer_t *buffer, int64_t value,
                                       sdkcc_error_t *error) {
  char text[32] = {0};
  const int length = snprintf(text, sizeof(text), "%" PRId64, value);
  if (length < 0 || (size_t)length >= sizeof(text)) {
    return sdkcc_internal_error_set(error, SDKCC_ERR_INTERNAL,
                                    SDKCC_ERROR_INTERNAL,
                                    "integer formatting failed");
  }
  return sdkcc_v1_buffer_append_string(
      buffer, (sdkcc_string_view_t){.data = text, .len = (size_t)length},
      error);
}

sdkcc_status_t sdkcc_v1_json_write_bool(sdkcc_buffer_t *buffer, bool value,
                                        sdkcc_error_t *error) {
  return sdkcc_v1_buffer_append_string(
      buffer, value ? SDKCC_STR_LITERAL("true") : SDKCC_STR_LITERAL("false"),
      error);
}
