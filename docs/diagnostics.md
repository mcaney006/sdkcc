# Diagnostics

Compiler failures carry a stable code, severity, message, source location, and
optional help text. Text is the default for terminals; JSON is selected with
`--diagnostic-format json` for CI and editor integrations.

```text
tests/specs/bad.yaml:12:9: error E1103: unsupported schema type "number"
  help: Milestone 1 supports string, integer, and boolean fields
```

Diagnostics go to stderr. Successful command output goes to stdout. Color is
only used for an interactive terminal and can be disabled with `NO_COLOR`.
