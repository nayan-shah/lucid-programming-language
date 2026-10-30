import { spawn } from "node:child_process";
import { mkdtemp, readdir, readFile, rm, writeFile } from "node:fs/promises";
import os from "node:os";
import path from "node:path";
import { StringDecoder } from "node:string_decoder";
import { fileURLToPath } from "node:url";
import { randomUUID } from "node:crypto";
import express from "express";
import { createOfflineAssistant, parseStructuredResponse } from "./assistant.js";

const here = path.dirname(fileURLToPath(import.meta.url));
const defaultCompilerPath = path.resolve(here, "..", "..", "lucid.exe");
const examplesPath = path.resolve(here, "..", "..", "examples", "valid");
const MAX_SOURCE_BYTES = 256 * 1024;
const MAX_OUTPUT_BYTES = 2 * 1024 * 1024;
const COMPILER_TIMEOUT_MS = 5000;
const MAX_AI_CONTEXT_BYTES = 64 * 1024;
const MAX_AI_PROMPT_CHARS = 4000;
const RUN_TIMEOUT_MS = 5 * 60 * 1000;
const RUN_SESSION_TTL_MS = 2 * 60 * 1000;
const INPUT_REQUEST_MARKER = "LUCID_INPUT_REQUEST\t";

function cleanupExpiredSessions(runSessions, latestRunBySource) {
  const now = Date.now();
  for (const [id, session] of runSessions) {
    if (now - session.updatedAt <= RUN_SESSION_TTL_MS) continue;
    session.child?.kill();
    clearTimeout(session.timer);
    if (session.tempDir) void rm(session.tempDir, { recursive: true, force: true });
    runSessions.delete(id);
    if (latestRunBySource.get(session.source) === id) latestRunBySource.delete(session.source);
  }
}

export class ApiError extends Error {
  constructor(status, message) {
    super(message);
    this.status = status;
  }
}

export function parseCompilerReport(stdout) {
  let report;
  try {
    report = JSON.parse(stdout);
  } catch {
    throw new ApiError(502, "The compiler returned invalid JSON.");
  }
  if (!report || typeof report !== "object" || report.schemaVersion !== "1.1") {
    throw new ApiError(502, "The compiler report has an unsupported schema.");
  }
  return report;
}

function invokeCompiler(executable, args, options = {}) {
  const timeoutMs = options.timeoutMs ?? COMPILER_TIMEOUT_MS;
  const maxOutputBytes = options.maxOutputBytes ?? MAX_OUTPUT_BYTES;
  const spawnProcess = options.spawnProcess ?? spawn;

  return new Promise((resolve, reject) => {
    let stdout = "";
    let stderr = "";
    let totalBytes = 0;
    let settled = false;
    const child = spawnProcess(executable, args, {
      cwd: path.dirname(executable),
      windowsHide: true,
      shell: false,
      stdio: ["ignore", "pipe", "pipe"],
    });

    const finish = (callback, value) => {
      if (settled) return;
      settled = true;
      clearTimeout(timer);
      callback(value);
    };

    const timer = setTimeout(() => {
      child.kill();
      finish(reject, new ApiError(504, "Compiler execution timed out."));
    }, timeoutMs);

    child.stdout.on("data", (chunk) => {
      totalBytes += chunk.length;
      if (totalBytes > maxOutputBytes) {
        child.kill();
        finish(reject, new ApiError(502, "Compiler output exceeded the size limit."));
      } else {
        stdout += chunk.toString("utf8");
      }
    });
    child.stderr.on("data", (chunk) => {
      if (Buffer.byteLength(stderr) + chunk.length <= 64 * 1024) {
        stderr += chunk.toString("utf8");
      }
    });
    child.on("error", (error) => {
      finish(reject, new ApiError(503, `Unable to start the compiler: ${error.message}`));
    });
    child.on("close", () => {
      finish(resolve, { stdout, stderr });
    });
  });
}

export function createApp(options = {}) {
  const app = express();
  const compilerPath = path.resolve(options.compilerPath ?? process.env.LUCID_COMPILER_PATH ?? defaultCompilerPath);
  const maxSourceBytes = options.maxSourceBytes ?? MAX_SOURCE_BYTES;
  const runSessions = new Map();
  const latestRunBySource = new Map();
  const cleanupTimer = setInterval(() => cleanupExpiredSessions(runSessions, latestRunBySource), 60_000);
  cleanupTimer.unref();
  const assistant = createOfflineAssistant();

  app.disable("x-powered-by");
  app.use(express.json({ limit: maxSourceBytes }));

  app.get("/api/health", (_req, res) => {
    res.json({ ok: true, compiler: path.basename(compilerPath) });
  });

  app.get("/api/ai/status", (_req, res) => {
    res.json({ available: true, provider: "Lucid Offline Assistant", model: null, fallbackModel: null, reason: "Offline — Ready" });
  });

  async function runAssistant(task, context) {
    return parseStructuredResponse(await assistant.generate(task, context), task);
  }

  function validateSource(source, limit = maxSourceBytes) {
    if (typeof source !== "string") throw new ApiError(400, "Request must include source as a string.");
    if (Buffer.byteLength(source, "utf8") > limit) {
      throw new ApiError(413, `Source exceeds the ${Math.floor(limit / 1024)} KiB limit for this request.`);
    }
  }

  async function compileSource(source, mode, invokeOptions = options) {
    const tempDir = await mkdtemp(path.join(os.tmpdir(), "lucid-"));
    try {
      const sourcePath = path.join(tempDir, "source.lc");
      await writeFile(sourcePath, source, "utf8");
      const args = [sourcePath, "--emit-json"];
      if (mode === "run") args.push("--run");
      const processResult = await invokeCompiler(compilerPath, args, invokeOptions);
      return parseCompilerReport(processResult.stdout);
    } finally {
      await rm(tempDir, { recursive: true, force: true });
    }
  }

  async function verifyDiagnostic(source, supplied, runSessionId) {
    if (supplied?.category === "RuntimeError") {
      const session = typeof runSessionId === "string" ? runSessions.get(runSessionId) : null;
      if (!session || latestRunBySource.get(source) !== runSessionId || session.state !== "completed" || session.source !== source || !session.report) {
        throw new ApiError(400, "Run the program to verify this runtime error. The execution result is unavailable or does not match this source.");
      }
      const observed = session.report.diagnostics.find((diagnostic) =>
        diagnostic.category === supplied.category && diagnostic.message === supplied.message &&
        diagnostic.line === supplied.line && diagnostic.column === supplied.column);
      if (!observed) throw new ApiError(400, "The selected runtime error was not observed in this execution.");
      return observed;
    }
    const report = await compileSource(source, supplied?.category === "RuntimeError" ? "run" : "compile");
    const actual = report.diagnostics.find((diagnostic) =>
      diagnostic.category === supplied?.category &&
      diagnostic.message === supplied?.message &&
      diagnostic.line === supplied?.line &&
      diagnostic.column === supplied?.column);
    if (!actual) throw new ApiError(400, "The selected diagnostic does not match the C++ compiler report for this source.");
    return actual;
  }

  app.get("/api/examples", async (_req, res, next) => {
    try {
      const files = (await readdir(examplesPath)).filter((file) => file.endsWith(".lc")).sort();
      const examples = await Promise.all(files.map(async (file) => ({
        name: file,
        source: await readFile(path.join(examplesPath, file), "utf8"),
      })));
      res.json(examples);
    } catch (error) {
      next(error);
    }
  });

  app.post("/api/compile", async (req, res, next) => {
    try {
      const { source, mode } = req.body ?? {};
      validateSource(source);
      if (mode !== "compile" && mode !== "run") {
        throw new ApiError(400, "Mode must be 'compile' or 'run'.");
      }
      res.status(200).json(await compileSource(source, mode));
    } catch (error) {
      next(error);
    }
  });

  app.post("/api/run-sessions", async (req, res, next) => {
    let tempDir;
    try {
      const { source } = req.body ?? {};
      validateSource(source);
      cleanupExpiredSessions(runSessions, latestRunBySource);
      const activeRuns = [...runSessions.values()].filter((session) =>
        session.state === "running" || session.state === "waiting_input").length;
      if (activeRuns >= (options.maxConcurrentRuns ?? 4)) {
        throw new ApiError(429, "Too many active interpreter sessions. Finish or cancel one before starting another.");
      }

      tempDir = await mkdtemp(path.join(os.tmpdir(), "lucid-run-"));
      const sourcePath = path.join(tempDir, "source.lc");
      await writeFile(sourcePath, source, "utf8");
      const child = (options.spawnProcess ?? spawn)(compilerPath, [
        sourcePath, "--run", "--emit-json", "--interactive-input",
      ], {
        cwd: path.dirname(compilerPath),
        windowsHide: true,
        shell: false,
        stdio: ["pipe", "pipe", "pipe"],
      });
      const id = randomUUID();
      const session = {
        id,
        source,
        child,
        tempDir,
        state: "running",
        inputVariable: null,
        report: null,
        error: null,
        stdout: "",
        stdoutDecoder: new StringDecoder("utf8"),
        stderrTail: "",
        outputBytes: 0,
        updatedAt: Date.now(),
        timer: null,
      };
      runSessions.set(id, session);
      latestRunBySource.set(source, id);
      session.timer = setTimeout(() => {
        session.error = "Interpreter session timed out.";
        session.state = "failed";
        session.updatedAt = Date.now();
        child.kill();
      }, options.runTimeoutMs ?? RUN_TIMEOUT_MS);
      child.stdout.on("data", (chunk) => {
        session.outputBytes += chunk.length;
        if (session.outputBytes > (options.maxOutputBytes ?? MAX_OUTPUT_BYTES)) {
          session.error = "Compiler output exceeded the size limit.";
          session.state = "failed";
          child.kill();
          return;
        }
        session.stdout += session.stdoutDecoder.write(chunk);
      });
      child.stderr.on("data", (chunk) => {
        const text = session.stderrTail + chunk.toString("utf8");
        const lines = text.split(/\r?\n/);
        session.stderrTail = lines.pop() ?? "";
        for (const line of lines) {
          if (line.startsWith(INPUT_REQUEST_MARKER)) {
            session.inputVariable = line.slice(INPUT_REQUEST_MARKER.length).trim();
            session.state = "waiting_input";
            session.updatedAt = Date.now();
          }
        }
      });
      child.stdin.on("error", (error) => {
        if (session.state === "cancelled" || session.state === "failed") return;
        session.error = `Unable to deliver interpreter input: ${error.message}`;
        session.state = "failed";
        session.updatedAt = Date.now();
        child.kill();
      });
      child.on("error", (error) => {
        session.error = `Unable to start the compiler: ${error.message}`;
        session.state = "failed";
        session.updatedAt = Date.now();
        clearTimeout(session.timer);
        void rm(tempDir, { recursive: true, force: true });
      });
      child.on("close", () => {
        clearTimeout(session.timer);
        session.stdout += session.stdoutDecoder.end();
        if (session.state !== "cancelled" && session.state !== "failed") {
          try {
            session.report = parseCompilerReport(session.stdout);
            session.state = "completed";
          } catch (error) {
            session.error = error.message;
            session.state = "failed";
          }
        }
        session.child = null;
        session.updatedAt = Date.now();
        void rm(tempDir, { recursive: true, force: true });
      });
      res.status(202).json({ sessionId: id });
    } catch (error) {
      if (tempDir) await rm(tempDir, { recursive: true, force: true });
      next(error);
    }
  });

  app.get("/api/run-sessions/:id", (req, res) => {
    const session = runSessions.get(req.params.id);
    if (!session) {
      res.status(404).json({ error: "Interpreter session was not found or has expired." });
      return;
    }
    session.updatedAt = Date.now();
    res.json({
      state: session.state,
      inputVariable: session.inputVariable,
      report: session.report,
      error: session.error,
    });
  });

  app.post("/api/run-sessions/:id/input", (req, res, next) => {
    try {
      const session = runSessions.get(req.params.id);
      if (!session) throw new ApiError(404, "Interpreter session was not found or has expired.");
      if (session.state !== "waiting_input" || !session.child) {
        throw new ApiError(409, "The interpreter is not waiting for input.");
      }
      const { value } = req.body ?? {};
      if (typeof value !== "string") throw new ApiError(400, "Input value must be text.");
      if (Buffer.byteLength(value, "utf8") > 4096 || /[\r\n]/.test(value)) {
        throw new ApiError(400, "Input must be one line and no more than 4 KiB.");
      }
      session.child.stdin.write(`${value}\n`);
      session.state = "running";
      session.inputVariable = null;
      session.updatedAt = Date.now();
      res.status(202).json({ accepted: true });
    } catch (error) {
      next(error);
    }
  });

  app.delete("/api/run-sessions/:id", (req, res) => {
    const session = runSessions.get(req.params.id);
    if (!session) {
      res.status(404).json({ error: "Interpreter session was not found or has expired." });
      return;
    }
    if (session.child) {
      session.state = "cancelled";
      session.updatedAt = Date.now();
      session.child.stdin.end();
      session.child.kill();
      clearTimeout(session.timer);
    }
    res.json({ cancelled: true });
  });

  app.post("/api/ai/generate", async (req, res, next) => {
    try {
      const { prompt } = req.body ?? {};
      if (typeof prompt !== "string" || !prompt.trim() || prompt.length > MAX_AI_PROMPT_CHARS) {
        throw new ApiError(400, `Describe a Lucid program using 1–${MAX_AI_PROMPT_CHARS} characters.`);
      }
      const result = await runAssistant("generate", { description: prompt.trim() });
      const report = await compileSource(result.source, "compile");
      result.compilerVerified = report.status === "success" && report.diagnostics.length === 0;
      result.compilerDiagnostics = report.diagnostics;
      res.json(result);
    } catch (error) {
      next(error);
    }
  });

  app.post("/api/ai/explain-diagnostic", async (req, res, next) => {
    try {
      const { source, diagnostic, runSessionId } = req.body ?? {};
      validateSource(source, MAX_AI_CONTEXT_BYTES);
      const actualDiagnostic = await verifyDiagnostic(source, diagnostic, runSessionId);
      res.json(await runAssistant("diagnostic", { source, diagnostic: actualDiagnostic }));
    } catch (error) {
      next(error);
    }
  });

  app.post("/api/ai/propose-repair", async (req, res, next) => {
    try {
      const { source, diagnostic, runSessionId } = req.body ?? {};
      validateSource(source, MAX_AI_CONTEXT_BYTES);
      const actualDiagnostic = await verifyDiagnostic(source, diagnostic, runSessionId);
      const result = await runAssistant("repair", { source, diagnostic: actualDiagnostic });
      if (result.source !== source) {
        const report = await compileSource(result.source, "compile");
        result.verified = report.status === "success" && report.diagnostics.length === 0;
        result.verificationDiagnostics = report.diagnostics;
      } else result.verified = false;
      result.before = source;
      res.json(result);
    } catch (error) {
      next(error);
    }
  });

  app.post("/api/ai/explain-ast", async (req, res, next) => {
    try {
      const { source, node } = req.body ?? {};
      validateSource(source, MAX_AI_CONTEXT_BYTES);
      if (!node || !Number.isInteger(node.id)) throw new ApiError(400, "Select an AST node from the compiler report.");
      const report = await compileSource(source, "compile");
      const findNode = (nodes) => {
        for (const candidate of nodes) {
          if (candidate.id === node.id) return candidate;
          const found = findNode(candidate.children.filter((child) => Number.isInteger(child.id)));
          if (found) return found;
        }
        return null;
      };
      const actualNode = findNode(report.ast.nodes);
      if (!actualNode || actualNode.type !== node.type || actualNode.line !== node.line || actualNode.column !== node.column) {
        throw new ApiError(400, "The selected AST node does not match the C++ compiler report for this source.");
      }
      res.json(await runAssistant("ast", { node: actualNode }));
    } catch (error) {
      next(error);
    }
  });

  app.post("/api/ai/explain-trace", async (req, res, next) => {
    try {
      const { source, events } = req.body ?? {};
      validateSource(source, MAX_AI_CONTEXT_BYTES);
      if (!Array.isArray(events) || events.length < 1 || events.length > 20 ||
          events.some((event) => typeof event !== "string" || event.length > 1000)) {
        throw new ApiError(400, "Select 1–20 actual runtime trace events.");
      }
      const report = await compileSource(source, "run");
      const actualEvents = report.execution.trace ?? [];
      if (events.some((event) => !actualEvents.includes(event))) {
        throw new ApiError(400, "The selected trace event does not match the C++ interpreter output for this source.");
      }
      res.json(await runAssistant("trace", { events }));
    } catch (error) {
      next(error);
    }
  });

  app.use((error, _req, res, _next) => {
    if (error instanceof SyntaxError && "body" in error) {
      res.status(400).json({ error: "Request body contains invalid JSON." });
      return;
    }
    if (error.type === "entity.too.large") {
      res.status(413).json({ error: "Request body exceeds the source-size limit." });
      return;
    }
    const status = error.status ?? 500;
    res.status(status).json({ error: status === 500 ? "Unexpected server error." : error.message });
  });

  app.locals.shutdown = async () => {
    clearInterval(cleanupTimer);
    const cleanup = [];
    for (const session of runSessions.values()) {
      clearTimeout(session.timer);
      if (session.child) {
        session.child.stdin.end();
        session.child.kill();
      }
      if (session.tempDir) cleanup.push(rm(session.tempDir, { recursive: true, force: true }));
    }
    await Promise.all(cleanup);
  };

  return app;
}
