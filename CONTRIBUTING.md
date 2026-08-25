# Contributing

Build and test the authoritative development preset:

```sh
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

Before submitting C or C++ changes, run `clang-format` on touched sources and
build the `asan` preset. Add a regression test that crosses the boundary which
failed. Code generator changes must pass the byte-for-byte determinism test.

Keep generated code obvious. Put complexity in typed compiler passes, the ABI,
or the runtime only when it has a measured or correctness-driven reason.

