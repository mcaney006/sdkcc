#include <minimal/minimal.hpp>
#include <sdkcc/test_transport.h>

#include <concepts>
#include <expected>
#include <iostream>

#define CHECK(expression)                                                      \
  do {                                                                         \
    if (!(expression)) {                                                       \
      std::cerr << __FILE__ << ':' << __LINE__                                 \
                << ": check failed: " #expression "\n";                        \
      return 1;                                                                \
    }                                                                          \
  } while (false)

int main() {
  sdkcc_error_t c_error{};
  sdkcc_test_transport_t *fake = nullptr;
  CHECK(sdkcc_v1_test_transport_create(nullptr, &fake, &c_error) == SDKCC_OK);
  const sdkcc_test_header_t headers[] = {
      {.name = SDKCC_STR_LITERAL("X-API-Key"),
       .value = SDKCC_STR_LITERAL("secret")},
  };
  const sdkcc_test_expectation_t expectation = {
      .method = SDKCC_HTTP_GET,
      .url = SDKCC_STR_LITERAL(
          "https://api.example.test/v1/users/cpp?verbose=false"),
      .headers = headers,
      .header_count = 1U,
      .response_status = 200,
      .response_body = SDKCC_BYTES_LITERAL("{\"id\":\"cpp\",\"name\":\"C++\","
                                           "\"age\":23,\"active\":true}"),
  };
  CHECK(sdkcc_v1_test_transport_expect(fake, &expectation, &c_error) ==
        SDKCC_OK);

  auto client = minimal::client::create(
      minimal::config{.api_key = "secret",
                      .transport = sdkcc_v1_test_transport_as_transport(fake)});
  CHECK(client.has_value());
  auto result = client->get_user("cpp", false);
  static_assert(std::same_as<decltype(result),
                             std::expected<minimal::user, sdkcc::error>>);
  CHECK(result.has_value());
  CHECK(result->id == "cpp");
  CHECK(result->name == "C++");
  CHECK(result->age == 23);
  CHECK(result->active);
  CHECK(!result->nickname.has_value());
  CHECK(sdkcc_v1_test_transport_verify(fake, &c_error) == SDKCC_OK);
  sdkcc_v1_test_transport_destroy(fake);
  sdkcc_v1_error_reset(&c_error);
  return 0;
}
