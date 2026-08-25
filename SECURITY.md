# Security

OpenAPI input, remote responses, and generated output paths are trust
boundaries. Report suspected vulnerabilities privately through GitHub's
security-advisory mechanism for this repository; do not include live secrets or
third-party customer data in a report.

The frontend rejects unsupported constructs instead of approximating their
meaning. Parsing and code generation enforce nesting, document, identifier, and
output-root checks. The runtime uses checked size arithmetic, bounded JSON
nesting, explicit buffer lengths, allocator provenance, and no ambient error
state. The test transport never logs request headers.

Remote `$ref` fetching is not implemented. When added, it will be
deny-by-default with explicit schemes, roots, byte/depth/node budgets, redirect
policy, and no credential forwarding.

