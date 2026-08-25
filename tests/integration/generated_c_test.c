#include <minimal/minimal.h>
#include <sdkcc/test_transport.h>

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

static int string_equals(sdkcc_owned_string_t value, const char *expected) {
  const size_t length = strlen(expected);
  return value.len == length &&
         (length == 0U || memcmp(value.data, expected, length) == 0);
}

int main(void) {
  sdkcc_error_t error = {0};
  sdkcc_test_transport_t *fake = NULL;
  CHECK(sdkcc_v1_test_transport_create(NULL, &fake, &error) == SDKCC_OK);
  const sdkcc_test_header_t get_headers[] = {
      {.name = SDKCC_STR_LITERAL("X-API-Key"),
       .value = SDKCC_STR_LITERAL("secret")},
  };
  const sdkcc_test_expectation_t get_expectation = {
      .method = SDKCC_HTTP_GET,
      .url = SDKCC_STR_LITERAL("https://api.example.test/v1/users/usr%2F123"
                               "?verbose=true"),
      .headers = get_headers,
      .header_count = 1U,
      .response_status = 200,
      .response_body =
          SDKCC_BYTES_LITERAL("{\"name\":\"Ada\",\"id\":\"usr/123\","
                              "\"active\":true,\"age\":42,\"nickname\":\"A\"}"),
  };
  CHECK(sdkcc_v1_test_transport_expect(fake, &get_expectation, &error) ==
        SDKCC_OK);

  const sdkcc_test_header_t post_headers[] = {
      {.name = SDKCC_STR_LITERAL("X-API-Key"),
       .value = SDKCC_STR_LITERAL("secret")},
      {.name = SDKCC_STR_LITERAL("Content-Type"),
       .value = SDKCC_STR_LITERAL("application/json")},
  };
  const sdkcc_test_expectation_t post_expectation = {
      .method = SDKCC_HTTP_POST,
      .url = SDKCC_STR_LITERAL("https://api.example.test/v1/users"),
      .headers = post_headers,
      .header_count = 2U,
      .body = SDKCC_BYTES_LITERAL(
          "{\"active\":true,\"age\":42,\"name\":\"Ada \\\"A\\\"\"}"),
      .response_status = 201,
      .response_body =
          SDKCC_BYTES_LITERAL("{\"id\":\"usr_456\",\"name\":\"Ada A\","
                              "\"age\":42,\"active\":true}"),
  };
  CHECK(sdkcc_v1_test_transport_expect(fake, &post_expectation, &error) ==
        SDKCC_OK);

  const minimal_config_t config = {
      .api_key = SDKCC_STR_LITERAL("secret"),
      .transport = sdkcc_v1_test_transport_as_transport(fake),
  };
  minimal_client_t *client = NULL;
  CHECK(minimal_v1_client_create(&config, &client, &error) == SDKCC_OK);

  const minimal_get_user_params_t get_params = {
      .id = SDKCC_STR_LITERAL("usr/123"),
      .has_verbose = true,
      .verbose = true,
  };
  minimal_get_user_response_t user = {0};
  CHECK(minimal_v1_get_user(client, &get_params, &user, &error) == SDKCC_OK);
  CHECK(string_equals(user.id, "usr/123"));
  CHECK(string_equals(user.name, "Ada"));
  CHECK(user.active);
  CHECK(user.age == 42);
  CHECK(user.has_nickname && string_equals(user.nickname, "A"));
  minimal_v1_model_user_reset(&user);

  const minimal_create_user_request_input_t input = {
      .active = true,
      .has_age = true,
      .age = 42,
      .name = SDKCC_STR_LITERAL("Ada \"A\""),
  };
  const minimal_create_user_params_t post_params = {.body = &input};
  minimal_create_user_response_t created = {0};
  CHECK(minimal_v1_create_user(client, &post_params, &created, &error) ==
        SDKCC_OK);
  CHECK(string_equals(created.id, "usr_456"));
  CHECK(!created.has_nickname);
  minimal_v1_model_user_reset(&created);

  CHECK(sdkcc_v1_test_transport_verify(fake, &error) == SDKCC_OK);
  minimal_v1_client_destroy(client);
  sdkcc_v1_test_transport_destroy(fake);
  sdkcc_v1_error_reset(&error);
  return 0;
}
