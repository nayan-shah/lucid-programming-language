# Lucid compiler JSON report schema

`lucid.exe <file.lc> --emit-json [--run]` emits exactly one JSON document to
stdout. The `--run`, `--emit-json`, and inspection flags may appear in any order
after the source path. With `--emit-json`, human-readable banners and inspection
tables are suppressed. The command exits with code 1 for compilation or runtime
errors, but still emits a parseable report.

The current schema version is `1.1`.

## Top-level fields

| Field | Meaning |
|---|---|
| `schemaVersion` | Contract version string |
| `requestId` | The source path's basename |
| `status` | `success` or `error`, including runtime failures |
| `lexer`, `parser`, `semantic` | Stage status and error count |
| `ast` | `available` or `unavailable`, plus AST node list |
| `execution` | `success`, `failed`, `skipped`, or `not_run`; output, trace, and runtime error are included when run |
| `tokens` | Actual lexer tokens, including the EOF token |
| `symbols` | Actual declarations and inferred types recorded by semantic analysis |
| `diagnostics` | Compiler/runtime diagnostics with source location and excerpt; correction is included only when the rejection explainer supplies one |
| `metrics` | Token count, AST node count/depth, block nesting depth, statement counts, and `stageTimings` |
| `sourceLength` | UTF-8 source byte count |

Stage statuses are `success`, `failed`, `skipped`, `unavailable`, or `not_run`.
Lexing and parsing always run. Semantic analysis is skipped if lexing or parsing
fails. The AST is marked unavailable after a lexer/parser failure rather than
presenting a recovered partial tree as a successful AST. Runtime execution is
skipped after any compile-time failure. Timings are `null`; the compiler does
not currently measure per-stage durations.

## Token object

- `type`: enum token type name
- `value`: token value / lexeme (escaped string content is decoded by the lexer)
- `line`, `column`: 1-based lexer location

The compiler does not currently expose token end positions or source ranges.

## AST

`ast.nodes` is a list of AST roots. Each AST node has an ID unique within the
report, `type`, 1-based `line` and `column`, and `children`. Child nodes carry
the same fields; metadata objects in `children` describe values such as names,
operators, and literals. End positions and source spans are not currently
available.
Input statement metadata includes the variable `name` and its declared
`inputType` (`text`, `number`, or `decimal`).

## Symbol object

- `name`, `type`, `scopeId`, `declaredAtLine`

Scope IDs uniquely identify lexical scopes within a compilation, including
scopes that have ended by the time analysis completes. Declaration columns and
explicit parent-scope links are not currently available.

## Diagnostic object

- `category`: `LexicalError`, `SyntaxError`, `SemanticError`, `TypeError`, or `RuntimeError`
- `message`: compiler/interpreter message
- `line`, `column`: 1-based source location (runtime location is the active AST expression/statement where available)
- `sourceLine`: source excerpt at that line
- `suggestion`: optional correction from the existing rejection explainer

## Execution trace

`execution.trace` contains strings emitted by the interpreter during actual
execution. Events currently cover declarations/assignments, output, branch
choices, loop iterations, function definition/calls, and function returns.
`execution.output` contains the interpreter's output lines. No browser-side
execution or reconstructed trace is used.

## Metrics

- `tokenCount`: number of emitted tokens including EOF
- `astNodeCount`: AST nodes in the valid parsed program
- `astDepth`: longest root-to-node path, with root statements at depth 1
- `maximumNestingDepth`: nested control/function blocks in the AST
- `statementTypes`: counts by AST statement node type
- `stageTimings`: `null` until genuine compiler timing instrumentation exists
