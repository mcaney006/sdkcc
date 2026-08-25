# Code generation

The C backend emits a public header, one source file, and a CMake target. The
C++ backend emits a header-only ergonomic layer linked to the same generated C
target. Files are written atomically beneath a validated output root.

Generated C uses explicit request views, response ownership, reset functions,
the runtime allocator, and the transport vtable. Generated C++ converts owned C
results into RAII `std::string` models and returns `std::expected`.

Code generation is deterministic by contract. Never add timestamps, random
guards, address-derived IDs, or iteration over unordered containers.

