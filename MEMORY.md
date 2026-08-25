# Memory and ownership

## Runtime

`sdkcc_string_view_t` and `sdkcc_buffer_view_t` are borrowed. The caller keeps
their backing storage alive for the duration documented by the receiving call.

`sdkcc_owned_string_t` and `sdkcc_buffer_t` own storage. Each value records the
allocator used for it. Reset them with `sdkcc_v1_owned_string_reset` and
`sdkcc_v1_buffer_reset`; reset is safe on a zero-initialized value.

Opaque requests own copies of their URL, headers, and body. A transport may
borrow those values only during its synchronous `send` callback. Transport
responses transfer an owned body to the caller.

Generated request models use borrowed views. Generated response models use
owned strings and provide an idempotent reset function. Generated clients copy
the small allocator and transport vtables but never own their external contexts.

## Compiler

Each compilation owns monotonic PMR arenas for parsed nodes, interned strings,
and IR. IDs remain stable until the compilation context is destroyed. No ID or
string view may escape the context into a later compilation.

