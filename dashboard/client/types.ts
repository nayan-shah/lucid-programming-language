export type StageStatus =
  | "success"
  | "failed"
  | "skipped"
  | "unavailable"
  | "not_run";

export interface TokenRecord {
  type: string;
  value: string;
  line: number;
  column: number;
}

export interface AstMetadata {
  [key: string]: string;
}

export interface AstNode {
  id: number;
  type: string;
  line: number;
  column: number;
  children: Array<AstNode | AstMetadata>;
}

export interface Diagnostic {
  category: string;
  message: string;
  line: number;
  column: number;
  sourceLine?: string;
  suggestion?: string;
}

export interface CompilerReport {
  schemaVersion: string;
  requestId: string;
  status: "success" | "error";
  lexer: { status: StageStatus; errorCount: number };
  parser: { status: StageStatus; errorCount: number };
  ast: { status: "available" | "unavailable"; nodes: AstNode[] };
  semantic: { status: StageStatus; errorCount: number };
  execution: {
    status: StageStatus;
    output?: string[];
    trace?: string[];
    error?: string;
    line?: number;
    column?: number;
  };
  tokens: TokenRecord[];
  symbols: Array<{
    name: string;
    type: string;
    scopeId: number;
    declaredAtLine: number;
  }>;
  diagnostics: Diagnostic[];
  metrics: {
    tokenCount: number;
    astNodeCount: number;
    astDepth: number;
    maximumNestingDepth: number;
    statementTypes: Record<string, number>;
    stageTimings: null | Record<string, number>;
  };
  sourceLength: number;
}

export interface ExampleProgram {
  name: string;
  source: string;
}

export interface AiStatus {
  available: boolean;
  provider: string | null;
  model: string | null;
  fallbackModel?: string | null;
  reason: string | null;
}

export interface AiCodeResponse {
  source: string;
  explanation: string;
  assumptions: string[];
  limitations: string[];
}

export interface AiExplanation {
  explanation: string;
  relevantRule?: string;
  observations?: string[];
}

export interface RunSessionResponse {
  state: "running" | "waiting_input" | "completed" | "failed" | "cancelled";
  inputVariable: string | null;
  report: CompilerReport | null;
  error: string | null;
}
