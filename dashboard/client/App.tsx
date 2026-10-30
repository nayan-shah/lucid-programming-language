import { useEffect, useMemo, useRef, useState } from "react";
import type { AiCodeResponse, AiExplanation, AiStatus, AstMetadata, AstNode, CompilerReport, Diagnostic, ExampleProgram, RunSessionResponse, StageStatus } from "./types";

type Panel = "diagnostics" | "tokens" | "ast" | "symbols" | "trace";

const panelLabels: Record<Panel, string> = {
  diagnostics: "Diagnostics",
  tokens: "Tokens",
  ast: "AST explorer",
  symbols: "Symbols & scopes",
  trace: "Execution trace",
};

function escapeHtml(text: string) {
  return text.replace(/[&<>"']/g, (char) => ({
    "&": "&amp;",
    "<": "&lt;",
    ">": "&gt;",
    "\"": "&quot;",
    "'": "&#39;",
  })[char] ?? char);
}

function highlightSource(source: string) {
  const pattern = /(note:.*)|("(?:\\.|[^"\\])*")|\b(give back|is not equal to|is equal to|is greater than|is less than|is at least|is at most|divided by|set|to|say|if|then|otherwise|end|repeat|while|create|using|ask|call|yes|no|and|or|not|plus|minus|times)\b/gi;
  let html = "";
  let cursor = 0;
  for (const match of source.matchAll(pattern)) {
    const index = match.index ?? 0;
    html += escapeHtml(source.slice(cursor, index));
    const value = match[0];
    const className = match[1] ? "hl-comment" : match[2] ? "hl-string" : "hl-keyword";
    html += `<span class="${className}">${escapeHtml(value)}</span>`;
    cursor = index + value.length;
  }
  html += escapeHtml(source.slice(cursor));
  return html || " ";
}

function stageLabel(status: StageStatus | "available" | "unavailable" | "pending") {
  if (status === "success" || status === "available") return "Passed";
  if (status === "failed") return "Failed";
  if (status === "skipped") return "Skipped";
  if (status === "unavailable") return "Unavailable";
  if (status === "pending") return "Pending";
  return "Not run";
}

function statusClass(status: StageStatus | "available" | "unavailable" | "pending") {
  return `status-${status}`;
}

function isAstNode(child: AstNode | AstMetadata): child is AstNode {
  return typeof (child as { id?: unknown }).id === "number";
}

function groupSymbols(symbols: CompilerReport["symbols"]) {
  const groups: Record<string, CompilerReport["symbols"]> = {};
  for (const symbol of symbols) {
    (groups[String(symbol.scopeId)] ??= []).push(symbol);
  }
  return Object.entries(groups).sort(([a], [b]) => Number(a) - Number(b));
}

function tokenCategory(type: string) {
  if (["NUMBER", "DECIMAL", "STRING", "YES", "NO"].includes(type)) return "literal";
  if (type === "IDENTIFIER") return "identifier";
  if (["PLUS", "MINUS", "TIMES", "DIVIDED_BY", "IS_EQUAL_TO", "IS_NOT_EQUAL_TO", "IS_GREATER_THAN", "IS_LESS_THAN", "IS_AT_LEAST", "IS_AT_MOST", "AND", "OR", "NOT"].includes(type)) return "operator";
  if (["LPAREN", "RPAREN"].includes(type)) return "delimiter";
  if (["NEWLINE", "EOF"].includes(type)) return "structure";
  return "keyword";
}

function reportStage(report: CompilerReport | null, stage: string): StageStatus | "available" | "unavailable" | "pending" {
  if (!report) return "pending";
  if (stage === "source") return "success";
  if (stage === "ast") return report.ast.status;
  return report[stage as "lexer" | "parser" | "semantic" | "execution"].status;
}

function isValidDraftReport(report: CompilerReport) {
  return report.status === "success" &&
    report.lexer.status === "success" &&
    report.parser.status === "success" &&
    report.semantic.status === "success" &&
    report.diagnostics.length === 0;
}

function sourceDiff(before: string, after: string) {
  const oldLines = before.split("\n");
  const newLines = after.split("\n");
  let start = 0;
  while (start < oldLines.length && start < newLines.length && oldLines[start] === newLines[start]) start += 1;
  let oldEnd = oldLines.length - 1;
  let newEnd = newLines.length - 1;
  while (oldEnd >= start && newEnd >= start && oldLines[oldEnd] === newLines[newEnd]) {
    oldEnd -= 1;
    newEnd -= 1;
  }
  if (start > oldEnd && start > newEnd) return "(no source changes)";
  return [
    ...oldLines.slice(start, oldEnd + 1).map((line) => `- ${line}`),
    ...newLines.slice(start, newEnd + 1).map((line) => `+ ${line}`),
  ].join("\n");
}

function AstTree({ node, onSelect }: { node: AstNode; onSelect: (node: AstNode) => void }) {
  const metadata = node.children.filter((child): child is AstMetadata => !isAstNode(child));
  const children = node.children.filter(isAstNode);
  const detail = metadata.flatMap((item) => Object.entries(item)).map(([key, value]) => `${key}: ${value}`).join(" · ");
  return (
    <details className="ast-node" open>
      <summary onClick={() => onSelect(node)}>
        <span className="ast-kind">{node.type}</span>
        <span className="ast-location">L{node.line}:C{node.column}</span>
        {detail && <span className="ast-detail">{detail}</span>}
      </summary>
      {children.length > 0 && (
        <div className="ast-children">
          {children.map((child) => <AstTree key={child.id} node={child} onSelect={onSelect} />)}
        </div>
      )}
    </details>
  );
}

export default function App() {
  const [source, setSource] = useState("");
  const [examples, setExamples] = useState<ExampleProgram[]>([]);
  const [selectedExample, setSelectedExample] = useState("");
  const [report, setReport] = useState<CompilerReport | null>(null);
  const [activePanel, setActivePanel] = useState<Panel>("diagnostics");
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState("");
  const [tokenFilter, setTokenFilter] = useState("all");
  const [tokenSearch, setTokenSearch] = useState("");
  const [selectedLine, setSelectedLine] = useState(0);
  const [aiStatus, setAiStatus] = useState<AiStatus | null>(null);
  const [aiBusy, setAiBusy] = useState("");
  const [aiError, setAiError] = useState("");
  const [generationPrompt, setGenerationPrompt] = useState("");
  const [generatedDraft, setGeneratedDraft] = useState<AiCodeResponse | null>(null);
  const [draftValidation, setDraftValidation] = useState<{ source: string; report: CompilerReport } | null>(null);
  const [draftValidationPending, setDraftValidationPending] = useState(false);
  const [draftValidationError, setDraftValidationError] = useState("");
  const [selectedDiagnostic, setSelectedDiagnostic] = useState<Diagnostic | null>(null);
  const [aiExplanation, setAiExplanation] = useState<AiExplanation | null>(null);
  const [selectedAstNode, setSelectedAstNode] = useState<AstNode | null>(null);
  const [selectedTraceEvent, setSelectedTraceEvent] = useState("");
  const [repairProposal, setRepairProposal] = useState<AiCodeResponse | null>(null);
  const [repairAttempts, setRepairAttempts] = useState(0);
  const [candidateReport, setCandidateReport] = useState<CompilerReport | null>(null);
  const [verificationBusy, setVerificationBusy] = useState(false);
  const [inputVariable, setInputVariable] = useState("");
  const [inputValue, setInputValue] = useState("");
  const [runSessionId, setRunSessionId] = useState("");
  const [evidenceRunSessionId, setEvidenceRunSessionId] = useState("");
  const runSessionRef = useRef("");
  const editorRef = useRef<HTMLTextAreaElement>(null);
  const gutterRef = useRef<HTMLDivElement>(null);
  const compilationVersion = useRef(0);
  const sourceHighlight = useMemo(() => highlightSource(source), [source]);
  const lineCount = Math.max(1, source.split("\n").length);
  const canRun = Boolean(
    report &&
    report.lexer.status === "success" &&
    report.parser.status === "success" &&
    report.semantic.status === "success" &&
    !loading,
  );

  useEffect(() => {
    fetch("/api/examples")
      .then(async (response) => {
        if (!response.ok) throw new Error("Examples are not available from the local compiler service.");
        return response.json() as Promise<ExampleProgram[]>;
      })
      .then((items) => {
        setExamples(items);
        if (items.length > 0) {
          setSource(items[0].source);
          setSelectedExample(items[0].name);
        }
      })
      .catch((requestError: Error) => setError(requestError.message));
  }, []);

  useEffect(() => {
    fetch("/api/ai/status")
      .then(async (response) => {
        if (!response.ok) throw new Error("Offline assistant status is unavailable.");
        setAiStatus(await response.json() as AiStatus);
      })
      .catch(() => setAiStatus({
        available: false,
        provider: null,
        model: null,
        reason: "Could not check offline assistant status.",
      }));
  }, []);

  async function cancelRunSession(sessionId = runSessionRef.current) {
    if (!sessionId) return;
    runSessionRef.current = "";
    setRunSessionId("");
    try {
      await fetch(`/api/run-sessions/${sessionId}`, { method: "DELETE" });
    } catch {
      // Cancellation is best-effort when the local backend is unreachable.
    }
  }

  async function submit() {
    const version = ++compilationVersion.current;
    await cancelRunSession();
    const submittedSource = source;
    setLoading(true);
    setError("");
    setReport(null);
    setEvidenceRunSessionId("");
    setSelectedDiagnostic(null);
    setSelectedAstNode(null);
    setSelectedTraceEvent("");
    setInputVariable("");
    setActivePanel("diagnostics");
    try {
      const response = await fetch("/api/compile", {
        method: "POST",
        headers: { "content-type": "application/json" },
        body: JSON.stringify({ source: submittedSource, mode: "compile" }),
      });
      const payload = await response.json();
      if (!response.ok) throw new Error(payload.error ?? `Compiler service failed (${response.status}).`);
      if (version === compilationVersion.current) setReport(payload as CompilerReport);
    } catch (requestError) {
      if (version === compilationVersion.current) {
        setError(requestError instanceof Error ? requestError.message : "Unable to reach the local compiler service.");
      }
    } finally {
      if (version === compilationVersion.current) setLoading(false);
    }
  }

  async function pollRunSession(sessionId: string, version: number) {
    while (version === compilationVersion.current && runSessionRef.current === sessionId) {
      const response = await fetch(`/api/run-sessions/${sessionId}`);
      const payload = await response.json() as RunSessionResponse & { error?: string };
      if (!response.ok) throw new Error(payload.error ?? `Interpreter service failed (${response.status}).`);
      if (payload.state === "waiting_input") {
        setInputVariable(payload.inputVariable ?? "input");
        setInputValue("");
        setLoading(false);
        return;
      }
      if (payload.state === "completed") {
        if (payload.report) {
          setReport(payload.report);
          setEvidenceRunSessionId(payload.report.diagnostics.some((item) => item.category === "RuntimeError") ? sessionId : "");
        }
        runSessionRef.current = "";
        setRunSessionId("");
        setInputVariable("");
        setLoading(false);
        return;
      }
      if (payload.state === "failed" || payload.state === "cancelled") {
        if (payload.report) setReport(payload.report);
        if (payload.error) setError(payload.error);
        runSessionRef.current = "";
        setRunSessionId("");
        setInputVariable("");
        setLoading(false);
        return;
      }
      await new Promise((resolve) => window.setTimeout(resolve, 180));
    }
  }

  async function startRun() {
    const version = ++compilationVersion.current;
    await cancelRunSession();
    setLoading(true);
    setError("");
    setReport(null);
    setEvidenceRunSessionId("");
    setSelectedDiagnostic(null);
    setSelectedAstNode(null);
    setSelectedTraceEvent("");
    setAiExplanation(null);
    setInputVariable("");
    setActivePanel("trace");
    try {
      const response = await fetch("/api/run-sessions", {
        method: "POST",
        headers: { "content-type": "application/json" },
        body: JSON.stringify({ source }),
      });
      const payload = await response.json();
      if (!response.ok) throw new Error(payload.error ?? `Interpreter service failed (${response.status}).`);
      runSessionRef.current = payload.sessionId as string;
      setRunSessionId(payload.sessionId as string);
      await pollRunSession(payload.sessionId as string, version);
    } catch (requestError) {
      if (version === compilationVersion.current) {
        setError(requestError instanceof Error ? requestError.message : "Unable to reach the local interpreter service.");
        setLoading(false);
      }
    }
  }

  async function submitRuntimeInput(event: React.FormEvent<HTMLFormElement>) {
    event.preventDefault();
    if (!runSessionId) return;
    setLoading(true);
    try {
      const response = await fetch(`/api/run-sessions/${runSessionId}/input`, {
        method: "POST",
        headers: { "content-type": "application/json" },
        body: JSON.stringify({ value: inputValue }),
      });
      const payload = await response.json();
      if (!response.ok) throw new Error(payload.error ?? `Input submission failed (${response.status}).`);
      setInputVariable("");
      await pollRunSession(runSessionId, compilationVersion.current);
    } catch (requestError) {
      setError(requestError instanceof Error ? requestError.message : "Unable to submit runtime input.");
      setLoading(false);
    }
  }

  async function cancelExecution() {
    await cancelRunSession();
    setInputVariable("");
    setLoading(false);
    setError("Execution cancelled.");
  }

  async function askAi(path: string, payload: object) {
    setAiBusy(path);
    setAiError("");
    setAiExplanation(null);
    try {
      const response = await fetch(`/api/ai/${path}`, {
        method: "POST",
        headers: { "content-type": "application/json" },
        body: JSON.stringify(payload),
      });
      const body = await response.json();
      if (!response.ok) throw new Error(body.error ?? `Assistant request failed (${response.status}).`);
      return body;
    } catch (requestError) {
      setAiError(requestError instanceof Error ? requestError.message : "The offline assistant request failed.");
      return null;
    } finally {
      setAiBusy("");
    }
  }

  async function generateDraft() {
    const result = await askAi("generate", { prompt: generationPrompt }) as AiCodeResponse | null;
    if (!result) return;
    setGeneratedDraft(result);
    setDraftValidation(null);
    setDraftValidationError("");
    setDraftValidationPending(true);
    try {
      const response = await fetch("/api/compile", {
        method: "POST",
        headers: { "content-type": "application/json" },
        body: JSON.stringify({ source: result.source, mode: "compile" }),
      });
      const payload = await response.json();
      if (!response.ok) throw new Error(payload.error ?? `Compiler service failed (${response.status}).`);
      setDraftValidation({ source: result.source, report: payload as CompilerReport });
    } catch (requestError) {
      setDraftValidationError(requestError instanceof Error ? requestError.message : "The Lucid compiler could not validate this draft.");
    } finally {
      setDraftValidationPending(false);
    }
  }

  async function explainDiagnostic() {
    const diagnostic = selectedDiagnostic ?? report?.diagnostics[0];
    if (!diagnostic) return;
    const result = await askAi("explain-diagnostic", { source, diagnostic, runSessionId: diagnostic.category === "RuntimeError" ? evidenceRunSessionId : undefined }) as AiExplanation | null;
    if (result) setAiExplanation(result);
  }

  async function proposeRepair() {
    const diagnostic = selectedDiagnostic ?? report?.diagnostics[0];
    if (!diagnostic || repairAttempts >= 2) return;
    setRepairAttempts((attempts) => attempts + 1);
    setCandidateReport(null);
    const result = await askAi("propose-repair", { source, diagnostic, runSessionId: diagnostic.category === "RuntimeError" ? evidenceRunSessionId : undefined }) as AiCodeResponse | null;
    if (result) setRepairProposal(result);
  }

  async function explainAstNode(node: AstNode) {
    setSelectedAstNode(node);
    const result = await askAi("explain-ast", { source, node }) as AiExplanation | null;
    if (result) setAiExplanation(result);
  }

  async function explainTraceEvent(event: string) {
    setSelectedTraceEvent(event);
    const result = await askAi("explain-trace", { source, events: [event] }) as AiExplanation | null;
    if (result) setAiExplanation(result);
  }

  async function verifyRepair() {
    if (!repairProposal) return;
    setVerificationBusy(true);
    setCandidateReport(null);
    try {
      const response = await fetch("/api/compile", {
        method: "POST",
        headers: { "content-type": "application/json" },
        body: JSON.stringify({ source: repairProposal.source, mode: "compile" }),
      });
      const payload = await response.json();
      if (!response.ok) throw new Error(payload.error ?? `Compiler service failed (${response.status}).`);
      setCandidateReport(payload as CompilerReport);
    } catch (requestError) {
      setAiError(requestError instanceof Error ? requestError.message : "The C++ compiler could not verify this proposal.");
    } finally {
      setVerificationBusy(false);
    }
  }

  function applyVerifiedRepair() {
    if (!repairProposal || !candidateReport || candidateReport.status !== "success") return;
    ++compilationVersion.current;
    void cancelRunSession();
    setSource(repairProposal.source);
    setReport(candidateReport);
    setSelectedExample("");
    setError("");
    setAiExplanation(null);
    setSelectedAstNode(null);
    setSelectedTraceEvent("");
    setInputVariable("");
    setActivePanel("diagnostics");
    setRepairProposal(null);
    setCandidateReport(null);
    setSelectedDiagnostic(null);
  }

  function updateSource(value: string) {
    ++compilationVersion.current;
    void cancelRunSession();
    setSource(value);
    setEvidenceRunSessionId("");
    setLoading(false);
    setReport(null);
    setError("");
    setSelectedExample("");
    setSelectedDiagnostic(null);
    setSelectedAstNode(null);
    setSelectedTraceEvent("");
    setAiExplanation(null);
    setAiError("");
    setInputVariable("");
    setRepairProposal(null);
    setCandidateReport(null);
    setRepairAttempts(0);
  }

  function loadExample(name: string) {
    const example = examples.find((item) => item.name === name);
    if (example) {
      updateSource(example.source);
      setSelectedExample(name);
    }
  }

  function navigateTo(line: number, column: number) {
    if (!line || !editorRef.current) return;
    const lines = source.split("\n");
    const offset = lines.slice(0, Math.max(0, line - 1)).reduce((sum, item) => sum + item.length + 1, 0);
    const position = Math.min(offset + Math.max(0, column - 1), source.length);
    editorRef.current.focus();
    editorRef.current.setSelectionRange(position, position);
    setSelectedLine(line);
  }

  const filteredTokens = (report?.tokens ?? []).filter((token) => {
    const filterMatch = tokenFilter === "all" || tokenCategory(token.type) === tokenFilter;
    const searchMatch = `${token.type} ${token.value}`.toLowerCase().includes(tokenSearch.toLowerCase());
    return filterMatch && searchMatch;
  });

  const stages = [
    { id: "source", label: "Source", panel: "diagnostics" as Panel },
    { id: "lexer", label: "Lexer", panel: "tokens" as Panel },
    { id: "parser", label: "Parser", panel: "ast" as Panel },
    { id: "ast", label: "AST", panel: "ast" as Panel },
    { id: "semantic", label: "Semantic", panel: "symbols" as Panel },
    { id: "execution", label: "Runtime", panel: "trace" as Panel },
  ];

  return (
    <main className="app-shell">
      <header className="topbar">
        <div className="brand-lockup">
          <div className="brand-mark" aria-hidden="true"><span /></div>
          <div>
            <div className="brand-name">LUCID <span>OBSERVATORY</span></div>
            <div className="brand-caption">Compiler and runtime learning lab</div>
          </div>
        </div>
        <div className="connection-state"><span className="live-dot" /> LOCAL COMPILER</div>
        <div className="topbar-right">C++14 <span className="topbar-divider">/</span> schema 1.1</div>
      </header>

      <section className="page-heading">
        <div>
          <div className="eyebrow">COMPILER DESIGN STUDIO <span>—</span> MILESTONE 02</div>
          <h1>See your program <em>think.</em></h1>
          <p>Trace controlled natural language from source to runtime, one compiler stage at a time.</p>
        </div>
        <div className="heading-note">Deterministic by design - no cloud required</div>
      </section>

      <section className="workspace" aria-label="Lucid source lab">
        <div className="editor-panel panel">
          <div className="panel-toolbar">
            <div className="file-tab"><span className="file-glyph">L</span> main.lc <span className="file-modified">●</span></div>
            <label className="example-select">
              <span>EXAMPLE</span>
              <select value={selectedExample} onChange={(event) => loadExample(event.target.value)}>
                <option value="">Choose program</option>
                {examples.map((example) => <option value={example.name} key={example.name}>{example.name}</option>)}
              </select>
              <span className="select-chevron">v</span>
            </label>
            <div className="editor-actions">
              <button className="button button-quiet" onClick={() => updateSource("")}>Clear</button>
              <button className="button button-quiet" onClick={() => loadExample(selectedExample || examples[0]?.name || "")} disabled={examples.length === 0}>Reset</button>
              <button className="button button-secondary" title="Run lexical, syntax, and semantic analysis on the current source." onClick={() => void submit()} disabled={loading}>
                {loading ? <span className="spinner" /> : <span aria-hidden="true" className="compile-glyph">↵</span>}
                Compile
              </button>
              <button className="button button-primary" title="Run the compiled source in the Lucid interpreter." onClick={() => void startRun()} disabled={!canRun}>
                <span aria-hidden="true" className="play-glyph">▶</span> Run
              </button>
            </div>
          </div>

          <div className="code-editor">
            <div className="line-gutter" ref={gutterRef} aria-hidden="true">
              {Array.from({ length: lineCount }, (_, index) => (
                <span className={selectedLine === index + 1 ? "active-line" : ""} key={index}>{index + 1}</span>
              ))}
            </div>
            <div className="editor-surface">
              <pre className="source-highlight" aria-hidden="true" dangerouslySetInnerHTML={{ __html: sourceHighlight }} />
              <textarea
                ref={editorRef}
                aria-label="Lucid source editor"
                spellCheck={false}
                wrap="off"
                value={source}
                placeholder={"note: Write a Lucid program or load an example\nset answer to 6 times 7\nsay answer"}
                onChange={(event) => updateSource(event.target.value)}
                onScroll={(event) => {
                  const area = event.currentTarget;
                  const highlight = area.previousElementSibling as HTMLElement | null;
                  if (highlight) {
                    highlight.scrollTop = area.scrollTop;
                    highlight.scrollLeft = area.scrollLeft;
                  }
                  if (gutterRef.current) gutterRef.current.scrollTop = area.scrollTop;
                }}
                onKeyDown={(event) => {
                  if ((event.ctrlKey || event.metaKey) && event.key === "Enter") {
                    event.preventDefault();
                    void submit();
                  }
                }}
              />
            </div>
          </div>

          {inputVariable && (
            <form className="runtime-input" onSubmit={(event) => void submitRuntimeInput(event)}>
              <div><strong>Runtime input required</strong><span>The interpreter paused at <code>ask {inputVariable}</code>.</span></div>
              <label htmlFor="runtime-input-value">{inputVariable}</label>
              <input id="runtime-input-value" aria-label={`Input for ${inputVariable}`} value={inputValue} onChange={(event) => setInputValue(event.target.value)} autoFocus />
              <button className="button button-primary" type="submit" disabled={loading}>Send input</button>
              <button className="button button-quiet" type="button" onClick={() => void cancelExecution()}>Cancel run</button>
            </form>
          )}

          <div className="editor-footer">
            <span><span className="footer-dot" /> Lucid language <span className="footer-divider">·</span> UTF-8</span>
            <span>{source.split("\n").length} lines <span className="footer-divider">·</span> {new TextEncoder().encode(source).length} bytes</span>
          </div>
        </div>

        <aside className="run-summary panel">
          <div className="summary-label">COMPILATION</div>
          <div className="summary-state">
            <span className={`summary-indicator ${report ? report.status === "success" ? "passed" : "failed" : loading ? "working" : ""}`} />
            <strong>{loading ? "Processing" : !report ? "Ready" : report.lexer.status === "failed" || report.parser.status === "failed" || report.semantic.status === "failed" ? "Build failed" : report.execution.status === "failed" ? "Runtime stopped" : report.execution.status === "success" ? "Execution complete" : "Build passed"}</strong>
          </div>
          <p>{loading ? "The compiler or interpreter is processing your source." : report ? (report.lexer.status === "failed" || report.parser.status === "failed" || report.semantic.status === "failed" ? `${report.diagnostics.length} compile-time diagnostic${report.diagnostics.length === 1 ? "" : "s"} need attention.` : report.execution.status === "failed" ? "Compilation passed; execution stopped with a runtime error." : "No compile-time errors found.") : "Compile to inspect the real compiler pipeline."}</p>
          <div className="summary-rule" />
          <div className="summary-stats">
            <div><span>Tokens</span><b>{report?.metrics.tokenCount ?? "—"}</b></div>
            <div><span>AST nodes</span><b>{report?.metrics.astNodeCount ?? "—"}</b></div>
            <div><span>Diagnostics</span><b className={report?.diagnostics.length ? "number-warn" : ""}>{report?.diagnostics.length ?? "—"}</b></div>
          </div>
          <div className="summary-foot">SOURCE OF TRUTH <span>C++14 ENGINE</span></div>
        </aside>
      </section>

      <section className="assistant-section panel" aria-label="Lucid Offline Assistant">
        <div className="assistant-heading">
          <div>
            <div className="eyebrow">OFFLINE LEARNING ASSISTANT</div>
            <h2>Lucid Offline Assistant <span>RULE BASED</span></h2>
            <p>Deterministic explanations and syntax templates, checked by the C++ compiler.</p>
          </div>
          <div className={`ai-status ${aiStatus?.available ? "ai-ready" : ""}`}>
            <span className="live-dot" /> {aiStatus?.available ? "Offline - Ready" : "Assistant unavailable"}
          </div>
        </div>
        <div className="ai-privacy">
          {aiStatus?.available
            ? "Runs locally with deterministic rules and templates. No API key or network connection is required."
            : aiStatus?.reason ?? "Checking local assistant status..."}
        </div>
        <div className="assistant-grid">
          <div className="ai-generation">
            <label htmlFor="generation-prompt">DESCRIBE A LUCID PROGRAM</label>
            <textarea
              id="generation-prompt"
              value={generationPrompt}
              maxLength={4000}
              onChange={(event) => setGenerationPrompt(event.target.value)}
              placeholder="Create a program that asks for a student's score and says whether they passed at 40 or above."
            />
            <button className="button button-secondary" onClick={() => void generateDraft()} disabled={aiStatus?.available !== true || aiBusy !== "" || !generationPrompt.trim()}>
              {aiBusy === "generate" ? <span className="spinner" /> : "✦"} Create example
            </button>
            {generatedDraft && (
              <div className="ai-result">
                <strong>
                  Template example · {draftValidationPending
                    ? "validating with Lucid compiler…"
                    : draftValidation?.source === generatedDraft.source
                        ? isValidDraftReport(draftValidation.report)
                          ? "compiler accepted · not run"
                          : "compiler found errors"
                      : "not compiler-validated"}
                </strong>
                <label>EXPLANATION</label>
                <p>{generatedDraft.explanation}</p>
                {generatedDraft.assumptions.length > 0 && <p className="ai-assumptions">Assumptions: {generatedDraft.assumptions.join("; ")}</p>}
                {generatedDraft.limitations.length > 0 && <p className="ai-assumptions">Limitations: {generatedDraft.limitations.join("; ")}</p>}
                <label>EXAMPLE SOURCE - EDITABLE</label>
                <textarea aria-label="Assistant source preview" value={generatedDraft.source} onChange={(event) => { setGeneratedDraft({ ...generatedDraft, source: event.target.value }); setDraftValidationError(""); }} />
                {draftValidationError && <div className="ai-error" role="alert">{draftValidationError}</div>}
                {!draftValidationPending && draftValidation?.source === generatedDraft.source &&
                  !isValidDraftReport(draftValidation.report) && (
                    <div className="ai-error" role="status">
                      {draftValidation.report.diagnostics.map((item) => item.message).join(" · ") || "The compiler did not accept this source."}
                    </div>
                  )}
                <button className="button button-primary" onClick={() => { updateSource(generatedDraft.source); setGeneratedDraft(null); }}>
                  Insert draft into editor
                </button>
              </div>
            )}
          </div>
          <div className="ai-context-tools">
            <label>EXPLAIN VERIFIED COMPILER DATA</label>
            <div className="ai-actions">
              <button className="button button-secondary" title="Explain the selected compiler diagnostic using its source and location." onClick={() => void explainDiagnostic()} disabled={aiStatus?.available !== true || aiBusy !== "" || !report?.diagnostics.length}>
                Explain diagnostic
              </button>
              <button className="button button-secondary" onClick={() => selectedAstNode && void explainAstNode(selectedAstNode)} disabled={aiStatus?.available !== true || aiBusy !== "" || !selectedAstNode}>
                Explain selected AST node
              </button>
              <button className="button button-secondary" onClick={() => selectedTraceEvent && void explainTraceEvent(selectedTraceEvent)} disabled={aiStatus?.available !== true || aiBusy !== "" || !selectedTraceEvent}>
                Explain selected trace event
              </button>
              <button className="button button-secondary" title="Create a source proposal only when a supported deterministic repair rule applies." onClick={() => void proposeRepair()} disabled={aiStatus?.available !== true || aiBusy !== "" || !report?.diagnostics.length || repairAttempts >= 2}>
                Propose fix ({repairAttempts}/2)
              </button>
            </div>
            {repairProposal && (
              <div className="ai-result repair-result">
                <strong>Repair proposal · attempt {repairAttempts}/2 · not yet compiler-validated</strong>
                <p>{repairProposal.explanation}</p>
                {repairProposal.assumptions.length > 0 && <p className="ai-assumptions">Assumptions: {repairProposal.assumptions.join("; ")}</p>}
                {repairProposal.limitations.length > 0 && <p className="ai-assumptions">Limitations: {repairProposal.limitations.join("; ")}</p>}
                <div className="repair-diff">
                  <label>BEFORE · CURRENT SOURCE</label>
                  <pre>{source || "(empty source)"}</pre>
                  <label>AFTER · EDITABLE PROPOSAL</label>
                  <textarea aria-label="Proposed repair source" value={repairProposal.source} onChange={(event) => { setRepairProposal({ ...repairProposal, source: event.target.value }); setCandidateReport(null); }} />
                  <label>LINE DIFF</label>
                  <pre aria-label="Repair diff">{sourceDiff(source, repairProposal.source)}</pre>
                </div>
                <div className="ai-actions">
                  <button className="button button-secondary" onClick={() => void verifyRepair()} disabled={verificationBusy || aiBusy !== ""}>
                    {verificationBusy ? <span className="spinner" /> : "↵"} Verify with C++ compiler
                  </button>
                  <button className="button button-primary" onClick={applyVerifiedRepair} disabled={!candidateReport || candidateReport.status !== "success"}>
                    Apply verified fix
                  </button>
                  <button className="button button-quiet" onClick={() => { setRepairProposal(null); setCandidateReport(null); }}>
                    Discard
                  </button>
                </div>
                {candidateReport && (
                  <div className={`repair-verdict ${candidateReport.status === "success" ? "verdict-good" : "verdict-bad"}`}>
                    {candidateReport.status === "success"
                      ? "C++ compiler validation passed. Applying remains a separate user action."
                      : `C++ compiler rejected this proposal: ${candidateReport.diagnostics.map((item) => item.message).join(" · ") || "compilation failed"}`}
                  </div>
                )}
              </div>
            )}
            {aiExplanation && (
              <div className="ai-result">
                <strong>Rule-based explanation · based on compiler output</strong>
                <p>{aiExplanation.explanation}</p>
                {aiExplanation.relevantRule && <p className="ai-assumptions">Language rule: {aiExplanation.relevantRule}</p>}
                {aiExplanation.observations?.map((item, index) => <p className="ai-assumptions" key={index}>· {item}</p>)}
              </div>
            )}
            {aiError && <div className="ai-error" role="alert">{aiError}</div>}
          </div>
        </div>
      </section>

      <section className="pipeline-section">
        <div className="section-heading">
          <div><div className="eyebrow">COMPILER JOURNEY</div><h2>Pipeline</h2></div>
          <div className="section-caption">Select a stage to inspect its output</div>
        </div>
        <div className="pipeline">
          {stages.map((stage, index) => {
            const status = reportStage(report, stage.id);
            const isSelected = activePanel === stage.panel;
            return (
              <div className="pipeline-step-wrap" key={stage.id}>
                <button className={`pipeline-step ${isSelected ? "selected" : ""}`} onClick={() => setActivePanel(stage.panel)}>
                  <span className={`stage-icon ${statusClass(status)}`}>
                    {status === "success" || status === "available" ? "✓" : status === "failed" ? "!" : status === "pending" ? "·" : "–"}
                  </span>
                  <span className="stage-name">{stage.label}</span>
                  <span className={`stage-status ${statusClass(status)}`}>{stageLabel(status)}</span>
                </button>
                {index < stages.length - 1 && <span className={`pipeline-connector ${report && status === "success" ? "connector-done" : ""}`} />}
              </div>
            );
          })}
        </div>
      </section>

      <section className="results-section">
        <div className="section-heading results-heading">
          <div><div className="eyebrow">INSPECT & DEBUG</div><h2>Compiler output</h2></div>
          {report && <span className={`result-badge ${report.status === "success" ? "result-good" : "result-bad"}`}>{report.status === "success" ? "● SUCCESS" : "● ERRORS FOUND"}</span>}
        </div>

        <div className="result-tabs" role="tablist">
          {(Object.keys(panelLabels) as Panel[]).map((panel) => (
            <button key={panel} role="tab" aria-selected={activePanel === panel} className={activePanel === panel ? "active" : ""} onClick={() => setActivePanel(panel)}>
              {panelLabels[panel]}
              {panel === "diagnostics" && report && <span className="tab-count">{report.diagnostics.length}</span>}
              {panel === "tokens" && report && <span className="tab-count">{report.tokens.length}</span>}
            </button>
          ))}
        </div>

        <div className="results-panel panel">
          {error && <div className="request-error"><span>!</span><div><strong>Local service unavailable</strong><p>{error} Start the API and Vite dev server as described in README.</p></div></div>}
          {!report && !error && !loading && <div className="empty-state"><span className="empty-orbit">◌</span><div><strong>Your compiler results will appear here.</strong><p>Compile the current Lucid source to inspect tokens, syntax, semantics, and runtime.</p></div></div>}
          {loading && <div className="loading-state"><span className="spinner" /> Waiting for the C++ compiler…</div>}
          {report && activePanel === "diagnostics" && (
            report.diagnostics.length === 0
              ? <div className="clean-state"><span>✓</span><div><strong>No diagnostics</strong><p>Lexer, parser, and semantic analysis found no errors.</p></div></div>
              : <div className="diagnostics-list">
                {report.diagnostics.map((diagnostic, index) => (
                  <DiagnosticRow key={`${diagnostic.category}-${diagnostic.line}-${index}`} diagnostic={diagnostic} onSelect={() => { setSelectedDiagnostic(diagnostic); navigateTo(diagnostic.line, diagnostic.column); }} />
                ))}
              </div>
          )}
          {report && activePanel === "tokens" && (
            <div className="token-view">
              <div className="inspector-toolbar">
                <span>{filteredTokens.length} <small>records</small></span>
                <input value={tokenSearch} onChange={(event) => setTokenSearch(event.target.value)} placeholder="Filter by token or lexeme" aria-label="Search tokens" />
                <select value={tokenFilter} onChange={(event) => setTokenFilter(event.target.value)} aria-label="Filter token category">
                  <option value="all">All categories</option>
                  {[...new Set(report.tokens.map((token) => tokenCategory(token.type)))].sort().map((category) => <option key={category}>{category}</option>)}
                </select>
              </div>
              {filteredTokens.length > 0 ? <div className="table-scroll">
                <table><thead><tr><th>TYPE</th><th>LEXEME</th><th>LINE</th><th>COLUMN</th></tr></thead>
                  <tbody>{filteredTokens.map((token, index) => (
                    <tr key={`${token.line}-${token.column}-${index}`} className={selectedLine === token.line ? "selected-row" : ""}>
                      <td><button type="button" className="token-jump" aria-label={`Go to ${token.type} token at line ${token.line}, column ${token.column}`} onClick={() => navigateTo(token.line, token.column)}><span className={`token-tag token-${token.type.toLowerCase()}`}>{token.type}</span></button></td>
                      <td className="token-lexeme">{token.value === "\n" ? "\\n" : token.value || "∅"}</td>
                      <td className="mono-cell">{token.line}</td><td className="mono-cell">{token.column}</td>
                    </tr>
                  ))}</tbody>
                </table>
              </div> : <div className="subpanel-empty">No tokens match this filter.</div>}
            </div>
          )}
          {report && activePanel === "ast" && (
            report.ast.status === "available" && report.ast.nodes.length > 0
              ? <div className="ast-tree">
                {report.ast.nodes.map((node) => <AstTree key={node.id} node={node} onSelect={(selected) => { setSelectedAstNode(selected); navigateTo(selected.line, selected.column); }} />)}
              </div>
              : <div className="empty-state"><span className="empty-orbit">✓</span><div><strong>AST unavailable</strong><p>{report.ast.status === "unavailable" ? "Parsing failed; a partial tree is not presented as successful." : "This valid program contains no statements."}</p></div></div>
          )}
          {report && activePanel === "symbols" && (
            report.symbols.length > 0
              ? <div className="symbol-scopes">
                {groupSymbols(report.symbols).map(([scope, symbols]) => (
                  <div className="scope-block" key={scope}>
                    <div className="scope-heading"><span className="scope-icon">▤</span><strong>Scope {scope}</strong><span>{symbols.length} symbol{symbols.length === 1 ? "" : "s"}</span></div>
                    <div className="table-scroll"><table><thead><tr><th>IDENTIFIER</th><th>INFERRED TYPE</th><th>DECLARED</th></tr></thead>
                      <tbody>{symbols.map((symbol) => <tr key={`${symbol.scopeId}-${symbol.name}-${symbol.declaredAtLine}`} onClick={() => navigateTo(symbol.declaredAtLine, 1)}><td className="symbol-name">{symbol.name}</td><td><span className="type-pill">{symbol.type}</span></td><td className="mono-cell">L{symbol.declaredAtLine}</td></tr>)}</tbody>
                    </table></div>
                  </div>
                ))}
              </div>
              : <div className="empty-state"><span className="empty-orbit">▤</span><div><strong>No symbols recorded</strong><p>Declared identifiers appear here after semantic analysis.</p></div></div>
          )}
          {report && activePanel === "trace" && (
            <div className="execution-view">
              <div className="execution-header"><div><span className="eyebrow">INTERPRETER</span><h3>{report.execution.status === "success" ? "Execution complete" : report.execution.status === "failed" ? "Runtime stopped" : report.execution.status === "skipped" ? "Execution skipped" : "Not executed"}</h3></div>
                <span className={`result-badge ${report.execution.status === "success" ? "result-good" : report.execution.status === "failed" ? "result-bad" : "result-neutral"}`}>{stageLabel(report.execution.status)}</span>
              </div>
              {report.execution.error && <div className="runtime-error"><strong>Runtime error</strong><code>{report.execution.error}</code></div>}
              <div className="runtime-columns">
                <div><div className="subpanel-heading">PROGRAM OUTPUT <span>{report.execution.output?.length ?? 0} lines</span></div>
                  {report.execution.output?.length ? <pre className="output-block">{report.execution.output.join("\n")}</pre> : <div className="subpanel-empty">No output produced.</div>}
                </div>
                <div><div className="subpanel-heading">EXECUTION TRACE <span>{report.execution.trace?.length ?? 0} events</span></div>
                  {report.execution.trace?.length ? <ol className="trace-list">{report.execution.trace.map((event, index) => <li key={`${event}-${index}`}><button className={`trace-event ${selectedTraceEvent === event ? "trace-event-selected" : ""}`} onClick={() => setSelectedTraceEvent(event)}><span>{String(index + 1).padStart(2, "0")}</span><code>{event}</code></button></li>)}</ol> : <div className="subpanel-empty">No runtime events available.</div>}
                </div>
              </div>
            </div>
          )}
        </div>
      </section>

      {report && <section className="analytics-section">
        <div className="section-heading"><div><div className="eyebrow">MEASURED FROM THIS COMPILATION</div><h2>Program profile</h2></div><span className="section-caption">Stage timings are unavailable in the current compiler.</span></div>
        <div className="analytics-grid">
          <MetricCard label="AST depth" value={report.metrics.astDepth} explanation="Longest root-to-node path in the reported AST." />
          <MetricCard label="Max nesting" value={report.metrics.maximumNestingDepth} explanation="Nested blocks across conditions, loops, and function declarations." />
          <MetricCard label="Source bytes" value={report.sourceLength} explanation="Bytes in the submitted UTF-8 source." />
          <MetricCard label="Diagnostics" value={report.diagnostics.length} explanation="Compiler and runtime diagnostics in this result." />
          <div className="distribution-card panel">
            <div className="metric-label">STATEMENT DISTRIBUTION</div>
            {Object.keys(report.metrics.statementTypes).length === 0
              ? <div className="subpanel-empty">No statements in this program.</div>
              : Object.entries(report.metrics.statementTypes).sort((a, b) => b[1] - a[1]).map(([type, count]) => {
                const max = Math.max(...Object.values(report.metrics.statementTypes), 1);
                return <div className="distribution-row" key={type}><span>{type}</span><div className="bar-track"><i style={{ width: `${(count / max) * 100}%` }} /></div><b>{count}</b></div>;
              })}
            <p>Counts include statement nodes present in the parsed AST.</p>
            <div className="metric-label diagnostic-distribution-title">DIAGNOSTIC CATEGORIES</div>
            {Object.entries(report.diagnostics.reduce<Record<string, number>>((counts, diagnostic) => {
              counts[diagnostic.category] = (counts[diagnostic.category] ?? 0) + 1;
              return counts;
            }, {})).sort((a, b) => b[1] - a[1]).map(([category, count]) => (
              <div className="distribution-row" key={category}><span>{category}</span><div className="bar-track"><i className="diagnostic-bar" style={{ width: `${(count / Math.max(report.diagnostics.length, 1)) * 100}%` }} /></div><b>{count}</b></div>
            ))}
            {report.diagnostics.length === 0 && <p>No diagnostic categories in this result.</p>}
          </div>
        </div>
      </section>}

      <footer className="app-footer"><span>LUCID <i>·</i> compiler observatory</span><span>Local-first learning tool <i>·</i> deterministic C++ engine</span></footer>
    </main>
  );
}

function DiagnosticRow({ diagnostic, onSelect }: { diagnostic: Diagnostic; onSelect: () => void }) {
  const severity = diagnostic.category === "RuntimeError" ? "runtime" : diagnostic.category === "TypeError" ? "type" : diagnostic.category === "SemanticError" ? "semantic" : "syntax";
  return (
    <button className="diagnostic-row" onClick={onSelect}>
      <span className={`diagnostic-icon diag-${severity}`}>{diagnostic.category === "RuntimeError" ? "×" : "!"}</span>
      <span className="diagnostic-content">
        <span className="diagnostic-meta"><b>{diagnostic.category}</b><span>Line {diagnostic.line}, column {diagnostic.column}</span></span>
        <span className="diagnostic-message">{diagnostic.message}</span>
        {diagnostic.sourceLine && <code className="diagnostic-source">{diagnostic.sourceLine}</code>}
        {diagnostic.suggestion && <span className="diagnostic-suggestion">Suggestion · {diagnostic.suggestion}</span>}
      </span>
      <span className="diagnostic-arrow">›</span>
    </button>
  );
}

function MetricCard({ label, value, explanation }: { label: string; value: number; explanation: string }) {
  return <div className="metric-card panel"><div className="metric-label">{label.toUpperCase()}</div><strong>{value.toLocaleString()}</strong><p>{explanation}</p></div>;
}
