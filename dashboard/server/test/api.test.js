import assert from "node:assert/strict";
import { EventEmitter } from "node:events";
import { PassThrough } from "node:stream";
import { after, before, test } from "node:test";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { createApp, parseCompilerReport } from "../app.js";

const here = path.dirname(fileURLToPath(import.meta.url));
const compilerPath = path.resolve(process.env.LUCID_COMPILER_PATH ?? path.resolve(here, "..", "..", "..", "lucid.exe"));
let server;
let baseUrl;

before(async () => {
  server = createApp({ compilerPath }).listen(0, "127.0.0.1");
  await new Promise((resolve) => server.once("listening", resolve));
  baseUrl = `http://127.0.0.1:${server.address().port}`;
});

after(async () => {
  await new Promise((resolve, reject) => server.close((error) => error ? reject(error) : resolve()));
});

async function compile(source, mode = "compile") {
  const response = await fetch(`${baseUrl}/api/compile`, {
    method: "POST",
    headers: { "content-type": "application/json" },
    body: JSON.stringify({ source, mode }),
  });
  return { response, report: await response.json() };
}

async function startServer(app) {
  const listening = app.listen(0, "127.0.0.1");
  await new Promise((resolve) => listening.once("listening", resolve));
  return {
    server: listening,
    url: `http://127.0.0.1:${listening.address().port}`,
  };
}

async function waitForSession(url, sessionId, expectedState) {
  for (let i = 0; i < 100; i += 1) {
    const response = await fetch(`${url}/api/run-sessions/${sessionId}`);
    const session = await response.json();
    if (session.state === expectedState || session.state === "failed" || session.state === "cancelled") return session;
    await new Promise((resolve) => setTimeout(resolve, 20));
  }
  throw new Error(`Interpreter session did not reach ${expectedState}.`);
}

test("real compiler returns tokens, AST, symbols, and runtime trace", async () => {
  const { response, report } = await compile("set total to 4\nsay total\n", "run");
  assert.equal(response.status, 200);
  assert.equal(report.status, "success");
  assert.equal(report.tokens.some((token) => token.type === "SET"), true);
  assert.equal(report.ast.status, "available");
  assert.equal(report.ast.nodes[0].type, "VariableDeclaration");
  assert.ok(report.symbols.some((symbol) => symbol.name === "total"));
  assert.deepEqual(report.execution.output, ["4"]);
  assert.ok(report.execution.trace.some((event) => event.includes("declare total")));
});

test("API executes comma-separated multi-parameter functions", async () => {
  const source = [
    "create combine using left, right, suffix",
    "    give back left plus right plus suffix",
    "end",
    "set answer to call combine using 2, 3, 4",
    "say answer",
    "",
  ].join("\n");
  const { response, report } = await compile(source, "run");
  assert.equal(response.status, 200);
  assert.equal(report.status, "success");
  assert.deepEqual(report.execution.output, ["9"]);
  assert.equal(report.symbols.some((symbol) => symbol.name === "left"), true);
});

test("invalid Lucid source returns compiler diagnostics and skips runtime", async () => {
  const { response, report } = await compile("set x to\n", "run");
  assert.equal(response.status, 200);
  assert.equal(report.status, "error");
  assert.equal(report.parser.status, "failed");
  assert.equal(report.ast.status, "unavailable");
  assert.equal(report.execution.status, "skipped");
  assert.ok(report.diagnostics.some((diagnostic) => diagnostic.category === "SyntaxError"));
});

test("semantic and runtime failures remain distinguishable", async () => {
  const semantic = await compile("say missing\n", "run");
  assert.equal(semantic.report.semantic.status, "failed");
  assert.equal(semantic.report.execution.status, "skipped");
  assert.ok(semantic.report.diagnostics.some((diagnostic) => diagnostic.category === "SemanticError"));

  const runtime = await compile("say 8 divided by 0\n", "run");
  assert.equal(runtime.report.semantic.status, "success");
  assert.equal(runtime.report.execution.status, "failed");
  assert.ok(runtime.report.diagnostics.some((diagnostic) => diagnostic.category === "RuntimeError"));
});

test("empty source compiles as an empty program", async () => {
  const { report } = await compile("");
  assert.equal(report.status, "success");
  assert.equal(report.ast.nodes.length, 0);
  assert.equal(report.metrics.tokenCount, 1);
});

test("escaped strings, unicode, and multiline output remain valid JSON", async () => {
  const source = "say \"quote: \\\" / path: \\\\ / cafÃ©\\nnext line\"\n";
  const { response, report } = await compile(source, "run");
  assert.equal(response.status, 200);
  assert.equal(report.status, "success");
  assert.deepEqual(report.execution.output, ["quote: \" / path: \\ / cafÃ©\nnext line"]);
});

test("invalid JSON request bodies receive a client error", async () => {
  const response = await fetch(`${baseUrl}/api/compile`, {
    method: "POST",
    headers: { "content-type": "application/json" },
    body: "{invalid",
  });
  assert.equal(response.status, 400);
  assert.match((await response.json()).error, /invalid JSON/i);
});

test("example catalog is read from the repository examples", async () => {
  const response = await fetch(`${baseUrl}/api/examples`);
  const examples = await response.json();
  assert.equal(response.status, 200);
  assert.ok(examples.some((example) => example.name === "01_basic.lc"));
});

test("invalid request JSON from the compiler is rejected", () => {
  assert.throws(() => parseCompilerReport("{not json"), /invalid JSON/i);
});

test("missing compiler executable returns a clear service error", async () => {
  const app = createApp({ compilerPath: path.join(here, "missing-lucid.exe") });
  const missingServer = app.listen(0, "127.0.0.1");
  await new Promise((resolve) => missingServer.once("listening", resolve));
  try {
    const response = await fetch(`http://127.0.0.1:${missingServer.address().port}/api/compile`, {
      method: "POST",
      headers: { "content-type": "application/json" },
      body: JSON.stringify({ source: "say 1\n", mode: "compile" }),
    });
    const body = await response.json();
    assert.equal(response.status, 503);
    assert.match(body.error, /Unable to start the compiler/);

    const run = await fetch(`http://127.0.0.1:${missingServer.address().port}/api/run-sessions`, {
      method: "POST", headers: { "content-type": "application/json" }, body: JSON.stringify({ source: "ask age as number\n" }),
    });
    const { sessionId } = await run.json();
    const failed = await waitForSession(`http://127.0.0.1:${missingServer.address().port}`, sessionId, "failed");
    assert.equal(failed.report, null);
    const explanation = await fetch(`http://127.0.0.1:${missingServer.address().port}/api/ai/explain-diagnostic`, {
      method: "POST", headers: { "content-type": "application/json" },
      body: JSON.stringify({
        source: "ask age as number\n", runSessionId: sessionId,
        diagnostic: { category: "RuntimeError", message: "Input for 'age' must be a valid number at line 1", line: 1, column: 1 },
      }),
    });
    assert.equal(explanation.status, 400);
  } finally {
    await app.locals.shutdown();
    await new Promise((resolve) => missingServer.close(resolve));
  }
});

test("oversized request body is rejected", async () => {
  const app = createApp({ compilerPath, maxSourceBytes: 128 });
  const limitedServer = app.listen(0, "127.0.0.1");
  await new Promise((resolve) => limitedServer.once("listening", resolve));
  try {
    const response = await fetch(`http://127.0.0.1:${limitedServer.address().port}/api/compile`, {
      method: "POST",
      headers: { "content-type": "application/json" },
      body: JSON.stringify({ source: "x".repeat(200), mode: "compile" }),
    });
    assert.equal(response.status, 413);
  } finally {
    await new Promise((resolve) => limitedServer.close(resolve));
  }
});

test("compiler timeout is reported", async () => {
  const neverFinishes = () => {
    const child = new EventEmitter();
    child.stdout = new PassThrough();
    child.stderr = new PassThrough();
    child.kill = () => true;
    return child;
  };
  const app = createApp({ compilerPath, timeoutMs: 20, spawnProcess: neverFinishes });
  const timeoutServer = app.listen(0, "127.0.0.1");
  await new Promise((resolve) => timeoutServer.once("listening", resolve));
  try {
    const response = await fetch(`http://127.0.0.1:${timeoutServer.address().port}/api/compile`, {
      method: "POST",
      headers: { "content-type": "application/json" },
      body: JSON.stringify({ source: "say 1\n", mode: "compile" }),
    });
    assert.equal(response.status, 504);
    assert.match((await response.json()).error, /timed out/i);
  } finally {
    await new Promise((resolve) => timeoutServer.close(resolve));
  }
});

test("compiler stdout output limit is enforced", async () => {
  const tooMuchOutput = () => {
    const child = new EventEmitter();
    child.stdout = new PassThrough();
    child.stderr = new PassThrough();
    child.kill = () => true;
    process.nextTick(() => {
      child.stdout.write("0123456789abcdef");
      child.emit("close", 0, null);
    });
    return child;
  };
  const app = createApp({ compilerPath, maxOutputBytes: 8, spawnProcess: tooMuchOutput });
  const limitedServer = app.listen(0, "127.0.0.1");
  await new Promise((resolve) => limitedServer.once("listening", resolve));
  try {
    const response = await fetch(`http://127.0.0.1:${limitedServer.address().port}/api/compile`, {
      method: "POST",
      headers: { "content-type": "application/json" },
      body: JSON.stringify({ source: "say 1\n", mode: "compile" }),
    });
    assert.equal(response.status, 502);
    assert.match((await response.json()).error, /output exceeded/i);
  } finally {
    await new Promise((resolve) => limitedServer.close(resolve));
  }
});

test("ask sessions pause for text input and resume the real interpreter", async () => {
  const response = await fetch(`${baseUrl}/api/run-sessions`, {
    method: "POST",
    headers: { "content-type": "application/json" },
    body: JSON.stringify({ source: "ask name\nsay name\n" }),
  });
  assert.equal(response.status, 202);
  const { sessionId } = await response.json();
  const waiting = await waitForSession(baseUrl, sessionId, "waiting_input");
  assert.equal(waiting.inputVariable, "name");

  const inputResponse = await fetch(`${baseUrl}/api/run-sessions/${sessionId}/input`, {
    method: "POST",
    headers: { "content-type": "application/json" },
    body: JSON.stringify({ value: "Ada Lovelace" }),
  });
  assert.equal(inputResponse.status, 202);
  const completed = await waitForSession(baseUrl, sessionId, "completed");
  assert.deepEqual(completed.report.execution.output, ["Ada Lovelace"]);
  assert.ok(completed.report.execution.trace.includes("ask name"));
});

test("typed numeric input classifies below, at, and above the adult threshold", async () => {
  const source = [
    "ask age as number",
    "if age is at least 18 then",
    '    say "adult"',
    "otherwise",
    '    say "minor"',
    "end",
    "",
  ].join("\n");
  const { report } = await compile(source);
  assert.equal(report.status, "success");
  assert.equal(report.semantic.status, "success");
  assert.equal(report.symbols.find(({ name }) => name === "age")?.type, "number");

  for (const [age, expected] of [[17, "minor"], [18, "adult"], [21, "adult"]]) {
    const startResponse = await fetch(`${baseUrl}/api/run-sessions`, {
      method: "POST",
      headers: { "content-type": "application/json" },
      body: JSON.stringify({ source }),
    });
    assert.equal(startResponse.status, 202);
    const { sessionId } = await startResponse.json();
    const waiting = await waitForSession(baseUrl, sessionId, "waiting_input");
    assert.equal(waiting.inputVariable, "age");
    const inputResponse = await fetch(`${baseUrl}/api/run-sessions/${sessionId}/input`, {
      method: "POST",
      headers: { "content-type": "application/json" },
      body: JSON.stringify({ value: String(age) }),
    });
    assert.equal(inputResponse.status, 202);
    const completed = await waitForSession(baseUrl, sessionId, "completed");
    assert.equal(completed.report.status, "success");
    assert.deepEqual(completed.report.execution.output, [expected]);
  }
});

test("ask sessions handle sequential prompts", async () => {
  const response = await fetch(`${baseUrl}/api/run-sessions`, {
    method: "POST",
    headers: { "content-type": "application/json" },
    body: JSON.stringify({ source: "ask first\nask second\nsay first\nsay second\n" }),
  });
  const { sessionId } = await response.json();
  let waiting = await waitForSession(baseUrl, sessionId, "waiting_input");
  assert.equal(waiting.inputVariable, "first");
  await fetch(`${baseUrl}/api/run-sessions/${sessionId}/input`, {
    method: "POST",
    headers: { "content-type": "application/json" },
    body: JSON.stringify({ value: "Ada" }),
  });
  waiting = await waitForSession(baseUrl, sessionId, "waiting_input");
  assert.equal(waiting.inputVariable, "second");
  await fetch(`${baseUrl}/api/run-sessions/${sessionId}/input`, {
    method: "POST",
    headers: { "content-type": "application/json" },
    body: JSON.stringify({ value: "Lovelace" }),
  });
  const completed = await waitForSession(baseUrl, sessionId, "completed");
  assert.deepEqual(completed.report.execution.output, ["Ada", "Lovelace"]);
});

test("ask sessions can be cancelled and input is bounded to one line", async () => {
  const response = await fetch(`${baseUrl}/api/run-sessions`, {
    method: "POST",
    headers: { "content-type": "application/json" },
    body: JSON.stringify({ source: "ask name\nsay name\n" }),
  });
  const { sessionId } = await response.json();
  await waitForSession(baseUrl, sessionId, "waiting_input");
  const invalidInput = await fetch(`${baseUrl}/api/run-sessions/${sessionId}/input`, {
    method: "POST",
    headers: { "content-type": "application/json" },
    body: JSON.stringify({ value: "one\ntwo" }),
  });
  assert.equal(invalidInput.status, 400);
  const oversizedInput = await fetch(`${baseUrl}/api/run-sessions/${sessionId}/input`, {
    method: "POST",
    headers: { "content-type": "application/json" },
    body: JSON.stringify({ value: "x".repeat(4097) }),
  });
  assert.equal(oversizedInput.status, 400);

  const cancelled = await fetch(`${baseUrl}/api/run-sessions/${sessionId}`, { method: "DELETE" });
  assert.equal(cancelled.status, 200);
  const session = await waitForSession(baseUrl, sessionId, "cancelled");
  assert.equal(session.state, "cancelled");
});

test("non-interactive API execution reports EOF instead of binding empty text", async () => {
  const { response, report } = await compile("ask name\nsay name\n", "run");
  assert.equal(response.status, 200);
  assert.equal(report.execution.status, "failed");
  assert.match(report.execution.error, /Input ended/i);
});

test("offline assistant is ready without API credentials", async () => {
  const response = await fetch(`${baseUrl}/api/ai/status`);
  assert.equal(response.status, 200);
  assert.equal((await response.json()).available, true);
});

test("offline assistant validates templates and explains actual compiler data", async () => {
  const generated = await fetch(`${baseUrl}/api/ai/generate`, { method: "POST", headers: { "content-type": "application/json" }, body: JSON.stringify({ prompt: "hello greeting" }) });
  const draft = await generated.json();
  assert.equal(generated.status, 200);
  assert.equal(draft.compilerVerified, true);
  const source = "say missing\n";
  const { report } = await compile(source);
  const explanation = await fetch(`${baseUrl}/api/ai/explain-diagnostic`, { method: "POST", headers: { "content-type": "application/json" }, body: JSON.stringify({ source, diagnostic: report.diagnostics[0] }) });
  assert.equal(explanation.status, 200);
  assert.match((await explanation.json()).explanation, /missing/);
  const functionTemplate = await fetch(`${baseUrl}/api/ai/generate`, { method: "POST", headers: { "content-type": "application/json" }, body: JSON.stringify({ prompt: "show a function that returns a value" }) });
  const functionDraft = await functionTemplate.json();
  assert.equal(functionDraft.compilerVerified, true);
  assert.match(functionDraft.source, /create double/);
});

test("offline assistant recognizes the compiler's top-level return diagnostic", async () => {
  const source = "give back 5\n";
  const { report } = await compile(source);
  const diagnostic = report.diagnostics.find((item) => item.category === "SemanticError");
  assert.ok(diagnostic);
  const explanation = await fetch(`${baseUrl}/api/ai/explain-diagnostic`, {
    method: "POST", headers: { "content-type": "application/json" },
    body: JSON.stringify({ source, diagnostic }),
  });
  assert.equal(explanation.status, 200);
  assert.match((await explanation.json()).explanation, /outside of a function body/i);
  const repair = await fetch(`${baseUrl}/api/ai/propose-repair`, {
    method: "POST", headers: { "content-type": "application/json" },
    body: JSON.stringify({ source, diagnostic }),
  });
  assert.equal(repair.status, 200);
  assert.equal((await repair.json()).source, source);
});

test("offline assistant rejects forged diagnostics and malformed requests", async () => {
  const response = await fetch(`${baseUrl}/api/ai/explain-diagnostic`, { method: "POST", headers: { "content-type": "application/json" }, body: JSON.stringify({ source: "say 1\n", diagnostic: { category: "Fake", message: "fake", line: 1, column: 1 } }) });
  assert.equal(response.status, 400);
  const malformed = await fetch(`${baseUrl}/api/ai/generate`, { method: "POST", headers: { "content-type": "application/json" }, body: JSON.stringify({ prompt: "" }) });
  assert.equal(malformed.status, 400);
});

test("offline assistant repair is compiler-checked and remains an explicit proposal", async () => {
  const source = "say missing\n";
  const { report } = await compile(source);
  const response = await fetch(`${baseUrl}/api/ai/propose-repair`, {
    method: "POST",
    headers: { "content-type": "application/json" },
    body: JSON.stringify({ source, diagnostic: report.diagnostics[0] }),
  });
  const proposal = await response.json();
  assert.equal(response.status, 200);
  assert.equal(proposal.before, source);
  assert.notEqual(proposal.source, source);
  assert.equal(proposal.verified, true);
  assert.equal(proposal.source.includes("set missing to 0"), true);
});

test("offline assistant explains the actual typed-input runtime diagnostic", async () => {
  const source = "ask age as number\nsay age\n";
  const start = await fetch(`${baseUrl}/api/run-sessions`, {
    method: "POST", headers: { "content-type": "application/json" },
    body: JSON.stringify({ source }),
  });
  const { sessionId } = await start.json();
  assert.equal(start.status, 202);
  await waitForSession(baseUrl, sessionId, "waiting_input");
  await fetch(`${baseUrl}/api/run-sessions/${sessionId}/input`, {
    method: "POST", headers: { "content-type": "application/json" },
    body: JSON.stringify({ value: "not-a-number" }),
  });
  const session = await waitForSession(baseUrl, sessionId, "completed");
  const diagnostic = session.report.diagnostics.find((item) => item.category === "RuntimeError");
  assert.ok(diagnostic);
  const response = await fetch(`${baseUrl}/api/ai/explain-diagnostic`, {
    method: "POST", headers: { "content-type": "application/json" },
    body: JSON.stringify({ source, diagnostic, runSessionId: sessionId }),
  });
  assert.equal(response.status, 200);
  assert.match((await response.json()).explanation, /valid number/i);
});

test("runtime diagnostics require the latest completed execution of the exact source", async () => {
  const source = "ask age as number\nsay age\n";
  const forged = { category: "RuntimeError", message: "Input for 'age' must be a valid number at line 1", line: 1, column: 1 };
  const explain = (body) => fetch(`${baseUrl}/api/ai/explain-diagnostic`, {
    method: "POST", headers: { "content-type": "application/json" }, body: JSON.stringify(body),
  });

  const noEvidence = await explain({ source, diagnostic: forged, success: true });
  assert.equal(noEvidence.status, 400);
  assert.match((await noEvidence.json()).error, /Run the program to verify/i);
  const unverifiedRepair = await fetch(`${baseUrl}/api/ai/propose-repair`, {
    method: "POST", headers: { "content-type": "application/json" },
    body: JSON.stringify({ source, diagnostic: forged, success: true }),
  });
  assert.equal(unverifiedRepair.status, 400);

  const compileFailure = await fetch(`${baseUrl}/api/run-sessions`, {
    method: "POST", headers: { "content-type": "application/json" }, body: JSON.stringify({ source: "ask age as number\nset broken to\n" }),
  });
  const failedCompileId = (await compileFailure.json()).sessionId;
  const compileResult = await waitForSession(baseUrl, failedCompileId, "completed");
  assert.ok(compileResult.report.diagnostics.some((item) => item.category === "SyntaxError"));
  assert.equal((await explain({ source: "ask age as number\nset broken to\n", diagnostic: forged, runSessionId: failedCompileId })).status, 400);

  const started = await fetch(`${baseUrl}/api/run-sessions`, {
    method: "POST", headers: { "content-type": "application/json" }, body: JSON.stringify({ source }),
  });
  const { sessionId } = await started.json();
  await waitForSession(baseUrl, sessionId, "waiting_input");
  await fetch(`${baseUrl}/api/run-sessions/${sessionId}/input`, {
    method: "POST", headers: { "content-type": "application/json" }, body: JSON.stringify({ value: "42" }),
  });
  const validResult = await waitForSession(baseUrl, sessionId, "completed");
  assert.equal(validResult.report.execution.status, "success");
  assert.equal((await explain({ source, diagnostic: forged, runSessionId: sessionId })).status, 400);

  const failedRun = await fetch(`${baseUrl}/api/run-sessions`, {
    method: "POST", headers: { "content-type": "application/json" }, body: JSON.stringify({ source }),
  });
  const failedId = (await failedRun.json()).sessionId;
  await waitForSession(baseUrl, failedId, "waiting_input");
  await fetch(`${baseUrl}/api/run-sessions/${failedId}/input`, {
    method: "POST", headers: { "content-type": "application/json" }, body: JSON.stringify({ value: "not-a-number" }),
  });
  const failedResult = await waitForSession(baseUrl, failedId, "completed");
  const actual = failedResult.report.diagnostics.find((item) => item.category === "RuntimeError");
  assert.ok(actual);
  assert.equal((await explain({ source: `${source} `, diagnostic: actual, runSessionId: failedId })).status, 400);
  assert.equal((await explain({ source, diagnostic: forged, runSessionId: sessionId })).status, 400);
  const verified = await explain({ source, diagnostic: actual, runSessionId: failedId });
  assert.equal(verified.status, 200);
  assert.match((await verified.json()).explanation, /valid number/i);

  const multipleInputsSource = "ask name\nask age as number\nsay name\nsay age\n";
  const multiStart = await fetch(`${baseUrl}/api/run-sessions`, {
    method: "POST", headers: { "content-type": "application/json" }, body: JSON.stringify({ source: multipleInputsSource }),
  });
  const multiId = (await multiStart.json()).sessionId;
  await waitForSession(baseUrl, multiId, "waiting_input");
  await fetch(`${baseUrl}/api/run-sessions/${multiId}/input`, {
    method: "POST", headers: { "content-type": "application/json" }, body: JSON.stringify({ value: "Ada" }),
  });
  await waitForSession(baseUrl, multiId, "waiting_input");
  await fetch(`${baseUrl}/api/run-sessions/${multiId}/input`, {
    method: "POST", headers: { "content-type": "application/json" }, body: JSON.stringify({ value: "bad-age" }),
  });
  const multiResult = await waitForSession(baseUrl, multiId, "completed");
  const secondInputError = multiResult.report.diagnostics.find((item) => item.category === "RuntimeError");
  assert.ok(secondInputError);
  assert.equal(secondInputError.line, 2);
  const multiExplanation = await explain({ source: multipleInputsSource, diagnostic: secondInputError, runSessionId: multiId });
  assert.equal(multiExplanation.status, 200);
  assert.match((await multiExplanation.json()).explanation, /valid number/i);

  const rerun = await fetch(`${baseUrl}/api/run-sessions`, {
    method: "POST", headers: { "content-type": "application/json" }, body: JSON.stringify({ source }),
  });
  const rerunId = (await rerun.json()).sessionId;
  await waitForSession(baseUrl, rerunId, "waiting_input");
  await fetch(`${baseUrl}/api/run-sessions/${rerunId}/input`, {
    method: "POST", headers: { "content-type": "application/json" }, body: JSON.stringify({ value: "17" }),
  });
  const rerunResult = await waitForSession(baseUrl, rerunId, "completed");
  assert.equal(rerunResult.report.execution.status, "success");
  assert.equal((await explain({ source, diagnostic: actual, runSessionId: failedId })).status, 400);
});

test("offline AST and trace explanations use compiler and interpreter records", async () => {
  const source = "say 5\n";
  const { report } = await compile(source, "run");
  const ast = await fetch(`${baseUrl}/api/ai/explain-ast`, {
    method: "POST", headers: { "content-type": "application/json" },
    body: JSON.stringify({ source, node: report.ast.nodes[0] }),
  });
  assert.match((await ast.json()).explanation, /output statement/i);
  const trace = await fetch(`${baseUrl}/api/ai/explain-trace`, {
    method: "POST", headers: { "content-type": "application/json" },
    body: JSON.stringify({ source, events: report.execution.trace }),
  });
  assert.equal((await trace.json()).observations.length, report.execution.trace.length);
});
