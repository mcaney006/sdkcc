# sdkcc

Compile an OpenAPI description into a native C23 SDK and a C++23 wrapper over
the same stable C ABI.

```sh
cmake --preset dev
cmake --build --preset dev
ctest --preset dev

./build/dev/sdkcc compile tests/specs/minimal.yaml \
  --output ./generated --lang c,cpp --library minimal
```

The current vertical slice is deliberately small, but real:

```text
OpenAPI 3.1 YAML/JSON -> parsed document -> semantic lowering -> NAIR
  -> deterministic C/C++ codegen -> C23 runtime -> fake transport
  -> JSON request/response -> typed result
```

It supports GET and POST operations, path/query parameters, JSON object bodies
and responses with string/integer/boolean fields, and header API-key auth. The
generated C SDK and C++ `std::expected` wrapper are compiled and exercised by
the integration test; generated endpoint code never calls a concrete HTTP
library.

This is the beginning of the compiler and ABI, not a claim of full OpenAPI
coverage. See [ARCHITECTURE.md](ARCHITECTURE.md) for the implemented boundary
and the next risks.

## Prerequisites

- CMake 3.28+
- Ninja
- a C23/C++23 compiler (Clang 18+ is the primary development lane)
- libyaml 0.2.x
- simdjson 4.x
- `pkg-config`

On macOS with Homebrew:

```sh
brew install cmake ninja libyaml simdjson pkgconf
```

On Debian/Ubuntu:

```sh
sudo apt-get install cmake ninja-build pkg-config libyaml-dev libsimdjson-dev
```

## Layout

- `compiler/` — C++23 compiler, frontend, NAIR, passes, and code generators
- `runtime/` — actual C23 runtime and stable versioned C symbols
- `include/sdkcc/` — small C++23 runtime wrapper
- `tests/` — compiler/runtime tests plus the nested native generated-SDK proof
- `benchmarks/` — dependency-free benchmark executables
- `docs/` — diagnostics and operational documentation

The working name is intentionally boring. Engineering comes first.
