# NAIR

Native API Intermediate Representation is independent of OpenAPI and of output
languages. Nodes use strong `TypeId`, `EndpointId`, and `StringId` values rather
than pointer identity.

The initial executable subset contains:

- module metadata and API-key authentication
- primitive string, signed 64-bit integer, and boolean types
- structures with required/optional fields
- GET/POST endpoints
- path and query parameters
- JSON request and success-response types

Backends consume only prepared NAIR. OpenAPI source locations are retained on
IR nodes for diagnostics. Later schema graph forms extend the type sum without
exposing frontend objects.

