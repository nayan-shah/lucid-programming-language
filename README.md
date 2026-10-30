# Lucid Compiler Observatory

Lucid is a local-first teaching compiler for the controlled natural-language Lucid language. The C++14 compiler includes a lexer, parser, AST, scoped symbol table, semantic checks, JSON reports, and an interpreter. The React dashboard displays actual compiler and runtime data. Its deterministic offline assistant uses local rules and fixed templates; it does not call a cloud service or language model.

## Requirements

- Windows 10 or 11 with PowerShell
- MinGW-w64 `g++` with C++14 support (or `mingw32-make` and `g++`)
- Node.js 22.12 or newer and npm
- No API key or network access is needed after npm dependencies are installed

## First setup and tests

Open PowerShell in the project root (`lucid`). Build the compiler and its test executable, then run the tests:

```powershell
g++ -std=c++14 -Wall -Wextra -O2 -I src src/main.cpp src/lexer/phrase_table.cpp src/lexer/lexer.cpp src/parser/parser.cpp src/semantic/symbol_table.cpp src/semantic/analyzer.cpp src/errors/error_types.cpp src/errors/rejection_explainer.cpp src/runtime/interpreter.cpp src/runtime/compiler_report.cpp -o lucid.exe
g++ -std=c++14 -Wall -Wextra -O2 -I src tests/test_main.cpp src/lexer/phrase_table.cpp src/lexer/lexer.cpp src/parser/parser.cpp src/semantic/symbol_table.cpp src/semantic/analyzer.cpp src/errors/error_types.cpp src/errors/rejection_explainer.cpp src/runtime/interpreter.cpp src/runtime/compiler_report.cpp -o test_lucid.exe
.\test_lucid.exe
```

If `mingw32-make` is installed, the repository Makefile provides `mingw32-make` and `mingw32-make test` for the same builds and C++ tests.

Install the dashboard dependencies once. This is the only setup step that needs the npm registry:

```powershell
cd .\dashboard
npm ci
```

## Start the dashboard

Use two PowerShell terminals. In each, start from the project root.

**Terminal 1 — local API:**

```powershell
cd .\dashboard
npm run dev:server
```

The API binds to `127.0.0.1:4173`. It finds `lucid.exe` at the project root. If the compiler is in a different location, set the path in this terminal before starting the API:

```powershell
$env:LUCID_COMPILER_PATH = "D:\path\to\lucid.exe"
npm run dev:server
```

**Terminal 2 — dashboard:**

```powershell
cd .\dashboard
npm run dev:client
```

Open <http://127.0.0.1:5173>. Keep both terminals open. To restart cleanly, press Ctrl+C in each terminal, then run both start commands again. Do not delete the user source or project files when restarting.

The Vite development proxy expects the API at port 4173. If that port is already occupied, stop the other process or update the API port and `dashboard/vite.config.ts` proxy together. Vite serves the dashboard on port 5173 by default.

## Try the compiler and assistant

From the project root, run a valid sample through compilation and execution:

```powershell
.\lucid.exe examples\valid\01_basic.lc --emit-json
.\lucid.exe examples\valid\01_basic.lc --run
```

In the dashboard, choose a sample, select **Compile**, inspect Diagnostics, Tokens, AST explorer, and Symbols & scopes, then select **Run** and inspect Program output and Execution trace. For an interactive example, load `examples\valid\03_phase2.lc`; enter requested values in the runtime input box. Each `ask` waits for one line. `ask name` reads text; `ask age as number` requires a whole number; `ask price as decimal` accepts a finite decimal. Invalid typed input and end-of-input are runtime errors, not compile errors. Use **Cancel run** to stop a waiting program.

Expected output for the valid examples:

| Program | Output lines |
|---|---|
| `01_basic.lc` | `Alice`, `26` |
| `02_conditionals.lc` | `It is hot outside!`, `Stay hydrated!` |
| `03_phase2.lc` | `1`, `2`, `3`, `5`, `4`, `3`, `2`, `1`, `14`, `Hello from greet!` |

The offline assistant is available in the dashboard with no credentials. To check it directly while the API is running, open another PowerShell terminal and call its local template endpoint:

```powershell
Invoke-RestMethod -Method Post -Uri http://127.0.0.1:4173/api/ai/generate -ContentType 'application/json' -Body (@{ prompt = 'print a greeting' } | ConvertTo-Json)
```

Templates are fixed examples and are checked with the real compiler. Diagnostic, AST, and trace explanations use report data from the Lucid executable. Repair proposals show the original and proposed source plus a diff; **Verify with C++ compiler** must pass before **Apply verified repair** is enabled. The assistant does not silently change editor contents.

Runtime diagnostic explanations and repair requests require a completed execution session. The API checks that the session is the latest run for the submitted source, that the stored source matches exactly, and that the exact diagnostic appears in the compiler's execution report. Sessions expire after two minutes without activity. A client supplied message by itself is never accepted as evidence. The assistant is a small deterministic rule/template system; it is not a general purpose or generative language model.

## Local API

All endpoints are served by the local Express process. Compilation reports follow [docs/json-schema.md](docs/json-schema.md).

| Method and route | Purpose |
|---|---|
| `GET /api/health` | Backend and configured compiler status |
| `GET /api/examples` | Valid sample programs |
| `GET /api/ai/status` | Offline assistant availability |
| `POST /api/compile` | Compile source; accepts `{ "source": "...", "mode": "compile" }` or mode `run` |
| `POST /api/run-sessions` | Start an interactive run; returns a session ID |
| `GET /api/run-sessions/:id` | Poll for input, completion, or failure and read the report |
| `POST /api/run-sessions/:id/input` | Send one line of runtime input |
| `DELETE /api/run-sessions/:id` | Cancel a waiting execution |
| `POST /api/ai/generate` | Create a fixed example template and compiler-check it |
| `POST /api/ai/explain-diagnostic` | Explain a compiler diagnostic; runtime diagnostics also require `runSessionId` |
| `POST /api/ai/propose-repair` | Return a separate repair proposal; runtime diagnostics also require `runSessionId` |
| `POST /api/ai/explain-ast` | Explain a node verified against the compiler's AST for the source |
| `POST /api/ai/explain-trace` | Explain events verified against a real interpreter trace |

The compiler is launched with an argument array and `shell: false`. Source size is limited to 256 KiB for compiler requests and 64 KiB for assistant context. The Vite development proxy targets API port 4173; if the API port changes, update `dashboard/vite.config.ts` as well.

## Dashboard checks

From `dashboard`, run the complete frontend and API suite and the production build:

```powershell
npm test
npm run build
```

`npm test` runs Vitest frontend tests and Node API tests. API tests invoke the real root `lucid.exe` for compilation, execution, input, and assistant validation. Run `..\test_lucid.exe` from `dashboard` for the C++ compiler suite.

## Common problems

- **API says it cannot start the compiler:** build `lucid.exe` in the project root or set `LUCID_COMPILER_PATH` to its absolute path in the API terminal, then restart the API.
- **Dashboard reports the local service is unavailable:** confirm both terminals are running and API port 4173 is free. Restart the API terminal first, then the Vite terminal.
- **Port already in use:** close the process using the port. The Vite proxy targets API port 4173; changing only the server port breaks API requests.
- **`npm ci` fails:** confirm Node.js is version 22.12 or newer and internet access is available for this one-time dependency install. Runtime assistant use itself needs no network.
- **Compiler reports a syntax error:** select the diagnostic to jump to its source line and read the compiler's actual message and suggestion.
- **Program waits for input:** provide one line in the runtime input field. Numeric input must match its declared type. Cancel the run if you want to stop waiting.
- **Input ends or has the wrong type:** the interpreter reports a runtime error and stops; compile-time analysis may still have succeeded.

## Classroom demonstration checklist

1. Start the API and dashboard in separate terminals; show the local status and **Offline — Ready** assistant state.
2. Compile and run `examples\valid\01_basic.lc`; show tokens, AST, symbols, output, and trace.
3. Load an invalid sample from `examples\invalid`, compile it, and select its diagnostic to locate the source.
4. Load the interactive sample, run it, and enter the requested values.
5. Request an assistant explanation or template. Optionally propose a repair, inspect its diff, verify it, and apply it only as a separate action.

## Compiler report and language

The report schema is documented in [docs/json-schema.md](docs/json-schema.md), and the supported syntax is described in [docs/grammar.md](docs/grammar.md). The compiler emits schema version 1.1. Source/token/AST locations are line and column based; AST end ranges and symbol parent-scope links are not part of the current report. Stage timings are `null` because the compiler does not currently measure them.

## Known limitations

- Functions accept zero or more comma-separated parameters and arguments. Return types are inferred when return expressions establish a consistent type; parameter-dependent and recursive cases remain `unknown`.
- The grammar has no unary minus token; arithmetic expressions can produce negative values, but negative literals must be expressed as a subtraction.
- The repeat and while interpreters stop after a fixed iteration limit, and function calls have a recursion limit.
- The dashboard is a development UI served by Vite; a production deployment/static hosting setup is not included.
- Automated component and API tests run in jsdom/Node. Browser visual checks depend on a browser automation tool being available in the environment.
