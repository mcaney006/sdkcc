# Transport contract

A transport is a context pointer plus a V1 function table. Its synchronous
`send` function receives an immutable opaque request and fills an owned response.
Generated code constructs requests only through runtime functions.

The in-memory test transport implements the contract now. It consumes ordered
expectations and compares method, URL, selected headers, and body byte-for-byte.
It proves serialization without the network.

A curl transport, capability discovery, streaming, and async submission are
later vertical slices. Generated code must remain unaware of the concrete
transport.

Threading in V1:

- immutable request views during `send`: borrowed for the callback only
- request builders and generated clients: thread-compatible, not concurrently
  mutable
- test transport: single-thread-bound
- system allocator: thread-safe to the extent of the platform allocator

