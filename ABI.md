# C ABI policy

The C ABI is the compatibility foundation. Every exported function starts with
`sdkcc_v1_`; C++ wrappers contain no independent transport or allocation state.

- Implementation-bearing structures are opaque.
- Public views never own memory.
- Public owned buffers carry their allocator and are reset by a versioned
  runtime function.
- Enums have fixed values. New values may be appended; existing values never
  change meaning.
- Callers zero-initialize public aggregate inputs and set documented fields.
- No public API uses compiler-specific C++ types, exceptions, RTTI, or mangled
  symbols.
- Export lists and generated-client compatibility become release gates before
  the first stable release.

There is no stable-release compatibility promise at version 0.1, but ABI
discipline starts now so stabilization is an audit, not a redesign.

