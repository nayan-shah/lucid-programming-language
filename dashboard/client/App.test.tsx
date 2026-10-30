import { afterEach, describe, expect, it, vi } from "vitest";
import { cleanup, render, screen, waitFor, within } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import App from "./App";
import type { AiCodeResponse, CompilerReport, ExampleProgram } from "./types";

afterEach(() => {
  cleanup();
  vi.unstubAllGlobals();
});

const example: ExampleProgram = { name: "test.lc", source: "set answer to 42\nsay answer\n" };

const compilerReport: CompilerReport = {
  schemaVersion: "1.1",
  requestId: "test.lc",
  status: "success",
  lexer: { status: "success", errorCount: 0 },
  parser: { status: "success", errorCount: 0 },
  ast: {
    status: "available",
    nodes: [{
      id: 1,
      type: "VariableDeclaration",
      line: 1,
      column: 1,
      children: [
        { name: "answer" },
        { id: 2, type: "Literal", line: 1, column: 17, children: [{ value: "42", kind: "number" }] },
      ],
    }],
  },
  semantic: { status: "success", errorCount: 0 },
  execution: { status: "skipped" },
  tokens: [
    { type: "SET", value: "set", line: 1, column: 1 },
    { type: "IDENTIFIER", value: "answer", line: 1, column: 5 },
    { type: "NUMBER", value: "42", line: 1, column: 17 },
  ],
  symbols: [{ name: "answer", type: "number", scopeId: 1, declaredAtLine: 1 }],
  diagnostics: [],
  metrics: {
    tokenCount: 4,
    astNodeCount: 3,
    astDepth: 2,
    maximumNestingDepth: 0,
    statementTypes: { VariableDeclaration: 1 },
    stageTimings: null,
  },
  sourceLength: 27,
};

const executionReport: CompilerReport = {
  ...compilerReport,
  execution: { status: "success", output: ["42"], trace: ["declare answer = 42", "say 42"] },
};

function stubFetch() {
  return stubFetchWithReport(compilerReport);
}

function stubFetchWithReport(report: CompilerReport, options: { aiAvailable?: boolean; inputRun?: boolean; generatedSource?: string; runReport?: CompilerReport; repairProposal?: AiCodeResponse; verificationReport?: CompilerReport } = {}) {
  let inputSent = false;
  const fetchMock = vi.fn(async (input: RequestInfo | URL, init?: RequestInit) => {
    const url = String(input);
    const request = init;
    if (url.endsWith("/api/examples")) {
      return new Response(JSON.stringify([example]), { status: 200 });
    }
    if (url.endsWith("/api/ai/status")) {
      return new Response(JSON.stringify({
        available: true,
        provider: "Lucid Offline Assistant",
        model: null,
        fallbackModel: null,
        reason: "Offline — Ready",
      }), { status: 200 });
    }
    if (url.endsWith("/api/ai/generate")) {
      return new Response(JSON.stringify({
        source: options.generatedSource ?? "say \"Hello\"\n",
        explanation: "Prints a greeting.",
        assumptions: [],
        limitations: [],
      }), { status: 200 });
    }
    if (url.endsWith("/api/ai/propose-repair")) {
      return new Response(JSON.stringify(options.repairProposal), { status: 200 });
    }
    if (url.endsWith("/api/compile") && request?.method === "POST") {
      const body = JSON.parse(String(request.body)) as { source?: string };
      const result = body.source === options.repairProposal?.source ? options.verificationReport ?? report : report;
      return new Response(JSON.stringify(result), { status: 200 });
    }
    if (url.endsWith("/api/run-sessions") && request?.method === "POST") {
      return new Response(JSON.stringify({ sessionId: "test-session" }), { status: 202 });
    }
    if (url.endsWith("/api/run-sessions/test-session/input")) {
      inputSent = true;
      return new Response(JSON.stringify({ accepted: true }), { status: 202 });
    }
    if (url.endsWith("/api/run-sessions/test-session") && request?.method === "DELETE") {
      return new Response(JSON.stringify({ cancelled: true }), { status: 200 });
    }
    if (url.endsWith("/api/run-sessions/test-session")) {
      if (options.inputRun && !inputSent) {
        return new Response(JSON.stringify({ state: "waiting_input", inputVariable: "name", report: null, error: null }), { status: 200 });
      }
      const runReport = options.runReport ?? (report.execution.status === "skipped" ? executionReport : report);
      return new Response(JSON.stringify({ state: "completed", inputVariable: null, report: runReport, error: null }), { status: 200 });
    }
    return new Response(JSON.stringify(report), { status: 200 });
  });
  vi.stubGlobal("fetch", fetchMock);
  return fetchMock;
}

describe("Compiler Observatory UI", () => {
  it("renders real-report data in tokens, AST, and symbol inspectors", async () => {
    const user = userEvent.setup();
    stubFetch();
    render(<App />);

    await waitFor(() => expect((screen.getByRole("textbox", { name: /source editor/i }) as HTMLTextAreaElement).value).toBe(example.source));
    await user.click(screen.getByRole("button", { name: /compile/i }));
    await screen.findByText("Build passed");

    await user.click(screen.getByRole("tab", { name: /tokens/i }));
    expect(screen.getByText("IDENTIFIER", { selector: ".token-tag" })).not.toBeNull();
    expect(screen.getByText("answer")).not.toBeNull();

    await user.click(screen.getByRole("tab", { name: /ast explorer/i }));
    expect(document.querySelector(".ast-tree .ast-kind")?.textContent).toBe("VariableDeclaration");
    expect(document.querySelector(".ast-tree .ast-location")?.textContent).toBe("L1:C1");

    await user.click(screen.getByRole("tab", { name: /symbols/i }));
    const symbolTable = screen.getByText("Scope 1").closest(".scope-block");
    expect(symbolTable).not.toBeNull();
    expect(within(symbolTable as HTMLElement).getByText("answer")).not.toBeNull();
    expect(within(symbolTable as HTMLElement).getByText("number")).not.toBeNull();
  });

  it("filters tokens, reports empty searches, and supports keyboard source navigation", async () => {
    const user = userEvent.setup();
    stubFetch();
    render(<App />);
    const editor = screen.getByRole("textbox", { name: /source editor/i }) as HTMLTextAreaElement;
    await user.click(screen.getByRole("button", { name: /compile/i }));
    await screen.findByText("Build passed");
    await user.click(screen.getByRole("tab", { name: /tokens/i }));

    const search = screen.getByRole("textbox", { name: /search tokens/i });
    await user.type(search, "no-such-token");
    expect(screen.getByText("No tokens match this filter.")).not.toBeNull();
    await user.clear(search);

    const jump = screen.getByRole("button", { name: "Go to SET token at line 1, column 1" });
    jump.focus();
    await user.keyboard("{Enter}");
    expect(document.activeElement).toBe(editor);
    expect(editor.selectionStart).toBe(0);
  });

  it("enables Run only after successful compilation and displays execution output", async () => {
    const user = userEvent.setup();
    const fetchMock = stubFetch();
    render(<App />);
    const runButton = screen.getByTitle("Run the compiled source in the Lucid interpreter.");
    expect((runButton as HTMLButtonElement).disabled).toBe(true);

    await user.click(screen.getByRole("button", { name: /compile/i }));
    await screen.findByText("Build passed");
    expect((runButton as HTMLButtonElement).disabled).toBe(false);

    await user.click(runButton);
    await waitFor(() => expect(fetchMock).toHaveBeenCalled());
    await user.click(screen.getByRole("tab", { name: /execution trace/i }));
    await waitFor(() => expect(document.querySelector(".output-block")?.textContent).toBe("42"));
  });

  it("shows compiler diagnostics and keeps Run disabled after failure", async () => {
    const user = userEvent.setup();
    const failedReport: CompilerReport = {
      ...compilerReport,
      status: "error",
      semantic: { status: "failed", errorCount: 1 },
      diagnostics: [{
        category: "SemanticError",
        message: "Undefined variable 'missing'",
        line: 2,
        column: 5,
        sourceLine: "say missing",
      }],
      execution: { status: "skipped" },
    };
    stubFetchWithReport(failedReport);
    render(<App />);

    await user.click(screen.getByRole("button", { name: /compile/i }));
    await screen.findByText("Build failed");
    expect(screen.getByText("Undefined variable 'missing'")).not.toBeNull();
    expect((screen.getByTitle("Run the compiled source in the Lucid interpreter.") as HTMLButtonElement).disabled).toBe(true);
  });

  it("distinguishes a runtime failure from a compilation failure", async () => {
    const user = userEvent.setup();
    const runtimeFailure: CompilerReport = {
      ...compilerReport,
      status: "error",
      diagnostics: [{ category: "RuntimeError", message: "Division by zero at line 1", line: 1, column: 1, sourceLine: "say 8 divided by 0" }],
      execution: { status: "failed", output: [], trace: [], error: "Division by zero at line 1", line: 1, column: 1 },
    };
    stubFetchWithReport(compilerReport, { runReport: runtimeFailure });
    render(<App />);
    await user.click(screen.getByRole("button", { name: /compile/i }));
    await screen.findByText("Build passed");
    await user.click(screen.getByTitle("Run the compiled source in the Lucid interpreter."));
    await waitFor(() => expect(screen.getAllByText("Runtime stopped").length).toBeGreaterThan(0));
    expect(screen.getByText("Compilation passed; execution stopped with a runtime error.")).not.toBeNull();
  });

  it("keeps a repair proposal separate until compiler verification and user approval", async () => {
    const user = userEvent.setup();
    const failedReport: CompilerReport = {
      ...compilerReport,
      status: "error",
      semantic: { status: "failed", errorCount: 1 },
      diagnostics: [{ category: "SemanticError", message: "Undefined variable 'missing'", line: 1, column: 5, sourceLine: "say missing" }],
      execution: { status: "skipped" },
    };
    const fixedSource = "set missing to 0\nsay missing\n";
    stubFetchWithReport(failedReport, {
      repairProposal: { source: fixedSource, explanation: "Added a declaration.", assumptions: [], limitations: [] },
      verificationReport: compilerReport,
    });
    render(<App />);
    const editor = screen.getByRole("textbox", { name: /source editor/i }) as HTMLTextAreaElement;
    await user.click(screen.getByRole("button", { name: /compile/i }));
    await screen.findByText("Build failed");
    await user.click(screen.getByRole("button", { name: /Propose fix/i }));
    await screen.findByRole("textbox", { name: /Proposed repair source/i });
    expect(editor.value).toBe(example.source);
    await user.click(screen.getByRole("button", { name: /Verify with C\+\+ compiler/i }));
    await screen.findByText(/C\+\+ compiler validation passed/);
    await user.click(screen.getByRole("button", { name: /Apply verified fix/i }));
    expect(editor.value).toBe(fixedSource);
  });

  it("keeps template-generated code in an editable preview until explicit insertion", async () => {
    const user = userEvent.setup();
    const generated = "say \"Draft\"\n";
    const fetchMock = stubFetchWithReport(compilerReport, { aiAvailable: true, generatedSource: generated });
    render(<App />);

    const editor = screen.getByRole("textbox", { name: /source editor/i }) as HTMLTextAreaElement;
    await waitFor(() => expect(editor.value).toBe(example.source));
    await user.type(screen.getByRole("textbox", { name: /describe a lucid program/i }), "Print a greeting.");
    await user.click(screen.getByRole("button", { name: /create example/i }));
    const preview = await screen.findByRole("textbox", { name: /Assistant source preview/i }) as HTMLTextAreaElement;
    expect(preview.value).toBe(generated);
    await screen.findByText(/Template example.*compiler accepted.*not run/);
    expect(fetchMock).toHaveBeenCalledWith("/api/compile", expect.objectContaining({
      method: "POST",
      body: JSON.stringify({ source: generated, mode: "compile" }),
    }));
    expect(editor.value).toBe(example.source);

    await user.clear(preview);
    await user.type(preview, "say \"Edited\"\n");
    await user.click(screen.getByRole("button", { name: /insert draft into editor/i }));
    expect(editor.value).toBe('say "Edited"\n');
    expect(fetchMock).toHaveBeenCalledWith("/api/ai/generate", expect.objectContaining({ method: "POST" }));
  });

  it("collects ask input in the dashboard and resumes the interpreter session", async () => {
    const user = userEvent.setup();
    const fetchMock = stubFetchWithReport(compilerReport, { inputRun: true });
    render(<App />);
    const editor = screen.getByRole("textbox", { name: /source editor/i }) as HTMLTextAreaElement;
    await waitFor(() => expect(editor.value).toBe(example.source));
    await user.clear(editor);
    await user.type(editor, "ask name\nsay name\n");
    await user.click(screen.getByRole("button", { name: /compile/i }));
    await screen.findByText("Build passed");
    await user.click(screen.getByTitle("Run the compiled source in the Lucid interpreter."));
    const input = await screen.findByRole("textbox", { name: "Input for name" });
    await user.type(input, "Ada");
    await user.click(screen.getByRole("button", { name: /send input/i }));
    await waitFor(() => expect(fetchMock).toHaveBeenCalledWith(
      "/api/run-sessions/test-session/input",
      expect.objectContaining({ method: "POST" }),
    ));
    await waitFor(() => expect(screen.getAllByText("Execution complete").length).toBeGreaterThan(0));
  });
});

