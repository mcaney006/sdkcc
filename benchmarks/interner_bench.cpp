#include <sdkcc/compiler/source/string_interner.hpp>

#include <chrono>
#include <cstddef>
#include <iostream>
#include <memory_resource>
#include <string>

int main() {
  constexpr std::size_t count = 100'000U;
  std::pmr::monotonic_buffer_resource arena;
  sdkcc::compiler::StringInterner interner{&arena};
  const auto begin = std::chrono::steady_clock::now();
  for (std::size_t index = 0U; index < count; ++index) {
    (void)interner.intern("field_" + std::to_string(index % 20'000U));
  }
  const auto elapsed = std::chrono::steady_clock::now() - begin;
  const auto nanoseconds =
      std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count();
  std::cout << "interner operations: " << count << '\n'
            << "unique strings: " << interner.size() << '\n'
            << "elapsed ns: " << nanoseconds << '\n'
            << "ns/op: "
            << static_cast<double>(nanoseconds) / static_cast<double>(count)
            << '\n';
}
