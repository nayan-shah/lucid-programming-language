// Lucid Offline Assistant — fully deterministic, no network calls.
// Deterministic explanations and syntax templates; this module makes no network calls.

const MAX_SOURCE_BYTES = 64 * 1024;

// ---------------------------------------------------------------------------
// Diagnostic task
// ---------------------------------------------------------------------------

function handleDiagnostic(context) {
  const { diagnostic, source } = context;
  if (!diagnostic || typeof diagnostic !== "object") {
    return JSON.stringify({
      explanation: "No diagnostic context was provided.",
      relevantRule: "N/A",
    });
  }
  if (typeof source === "string" && Buffer.byteLength(source, "utf8") > MAX_SOURCE_BYTES) {
    return JSON.stringify({
      explanation: "The source context is too large to process.",
      relevantRule: "N/A",
    });
  }

  const { category = "Unknown", message = "", line = 0, column = 0 } = diagnostic;

  if (category === "SemanticError") {
    // Undeclared variable
    let m = message.match(/[Uu]ndefined variable '([^']+)'/) ||
             message.match(/[Uu]ndeclared identifier '([^']+)'/);
    if (m) {
      const name = m[1];
      return JSON.stringify({
        explanation: `The variable \`${name}\` is used on line ${line} but has not been declared. In Lucid, every variable must be declared with \`set ${name} to VALUE\` before it can be used.`,
        relevantRule: "Every identifier must be declared with `set` before use (EBNF: varDecl).",
      });
    }

    // Duplicate declaration
    m = message.match(/[Vv]ariable '([^']+)' is already declared/) ||
        message.match(/[Ff]unction '([^']+)' is already declared/) ||
        message.match(/[Aa]lready declared '([^']+)'/) ||
        message.match(/[Dd]uplicate declaration '([^']+)'/);
    if (m) {
      const name = m[1];
      return JSON.stringify({
        explanation: `The identifier \`${name}\` has already been declared in this scope. Each variable name may only be declared once. Remove the duplicate \`set ${name} to\` statement or rename one of them.`,
        relevantRule: "Each identifier may be declared at most once per scope (EBNF: varDecl).",
      });
    }

    // Type mismatch
    if (/[Tt]ype mismatch/.test(message) || /[Cc]annot (reassign|compare|change type|assign|use)/.test(message)) {
      return JSON.stringify({
        explanation: `A type mismatch was detected at line ${line}: '${message}'. In Lucid, values must be of a compatible type for the operation being performed. Check that you are not mixing numbers, decimals, text, and booleans in incompatible ways.`,
        relevantRule: "Operands and assignments must have compatible types (EBNF: expression).",
      });
    }

    // Return outside function
    if (/[Rr]eturn outside (function|body)/.test(message) || /[Rr]eturn statement is only valid inside a function/.test(message)) {
      return JSON.stringify({
        explanation: `A \`give back\` statement was found outside of a function body at line ${line}. In Lucid, \`give back\` may only appear inside a \`create ... end\` function declaration.`,
        relevantRule: "`give back` is only valid inside a function body (EBNF: returnStmt).",
      });
    }

    // Undeclared function
    m = message.match(/[Uu]ndefined function '([^']+)'/);
    if (m) {
      const name = m[1];
      return JSON.stringify({
        explanation: `The function \`${name}\` was called at line ${line} but has not been declared. In Lucid, functions must be declared with \`create ${name} ...\` before they can be called.`,
        relevantRule: "Functions must be declared with `create` before being called (EBNF: funcDecl).",
      });
    }

    // Generic SemanticError fallback
    return JSON.stringify({
      explanation: `A semantic error occurred at line ${line}, column ${column}: '${message}'. This means the program's structure is syntactically valid but violates a language rule such as type correctness or scoping.`,
      relevantRule: "Semantic rules govern identifier scope, type compatibility, and control flow (EBNF: program).",
    });
  }

  if (category === "SyntaxError") {
    return JSON.stringify({
      explanation: `The parser encountered a syntax error at line ${line}, column ${column}: '${message}'. This usually means a keyword is missing, misplaced, or misspelled, or that the statement is not complete.`,
      relevantRule: "Statements must follow the EBNF grammar exactly.",
    });
  }

  if (category === "LexicalError") {
    return JSON.stringify({
      explanation: `The lexer encountered an unrecognized token at line ${line}, column ${column}: '${message}'. Check for typos, unsupported characters, or invalid symbol combinations that the Lucid lexer cannot classify.`,
      relevantRule: "All tokens must match a known Lucid lexeme pattern (EBNF: token).",
    });
  }

  if (category === "TypeError") {
    return JSON.stringify({
      explanation: `A type error was reported at line ${line}, column ${column}: '${message}'. In Lucid, operations require operands of the correct type. Ensure that numeric operations use numbers or decimals, and that boolean conditions use boolean expressions.`,
      relevantRule: "Operand types must match the required type for each operator (EBNF: expression).",
    });
  }

  if (category === "RuntimeError") {
    if (/[Dd]ivision by zero/.test(message) || /[Dd]ivide.*by.*zero/.test(message)) {
      return JSON.stringify({
        explanation: `A division-by-zero error occurred at line ${line}: '${message}'. The denominator evaluated to zero at runtime. Add a condition to guard the division: check that the denominator \`is not equal to 0\` before dividing.`,
        relevantRule: "Division requires a non-zero denominator at runtime (EBNF: expression, operator: divided by).",
      });
    }
    if (/[Ii]nput.*must be a valid (number|decimal)/.test(message) || /[Ii]nput.*not.*number/.test(message) || /[Ii]nvalid.*number/.test(message) || /[Cc]annot.*convert/.test(message)) {
      return JSON.stringify({
        explanation: `A runtime input error occurred at line ${line}: '${message}'. The value entered for a typed \`ask ... as number\` or \`ask ... as decimal\` prompt could not be converted to the required numeric type. Ensure the user provides a valid whole number or decimal value.`,
        relevantRule: "Typed `ask` input must parse to the requested numeric type at runtime (EBNF: inputStmt).",
      });
    }
    if (/[Ii]nput.*ended/.test(message) || /[Ee]nd.*input/.test(message)) {
      return JSON.stringify({
        explanation: `A runtime error occurred at line ${line}: '${message}'. The interpreter reached an \`ask\` statement but no more input was available. If running non-interactively, ensure the program receives all required inputs.`,
        relevantRule: "Each `ask` statement requires one line of input from the user at runtime (EBNF: inputStmt).",
      });
    }
    return JSON.stringify({
      explanation: `A runtime error occurred: '${message}'. The interpreter stopped execution at line ${line}. Check the program logic and any input values that could cause unexpected behavior.`,
      relevantRule: "Runtime errors occur when program execution violates interpreter constraints.",
    });
  }

  // Unknown category
  return JSON.stringify({
    explanation: `The compiler reported: '${message}' (${category} at line ${line}, column ${column}).`,
    relevantRule: "N/A",
  });
}

// ---------------------------------------------------------------------------
// Repair task
// ---------------------------------------------------------------------------

function handleRepair(context) {
  const { diagnostic, source } = context;
  if (!diagnostic || typeof source !== "string") {
    return JSON.stringify({
      source: source ?? "",
      explanation: "No diagnostic or source context was provided for repair.",
      assumptions: [],
      limitations: ["Manual inspection required."],
    });
  }
  if (Buffer.byteLength(source, "utf8") > MAX_SOURCE_BYTES) {
    return JSON.stringify({
      source,
      explanation: "The source is too large to process automatically.",
      assumptions: [],
      limitations: ["Reduce source size and try again."],
    });
  }

  const { category = "Unknown", message = "", line = 0, suggestion = "" } = diagnostic;

  // Rule 1: Undeclared variable
  if (category === "SemanticError") {
    let m = message.match(/[Uu]ndefined variable '([^']+)'/) ||
             message.match(/[Uu]ndeclared identifier '([^']+)'/);
    if (m) {
      const name = m[1];
      const lines = source.split("\n");
      const fixed = [`set ${name} to 0`, ...lines].join("\n");
      return JSON.stringify({
        source: fixed,
        explanation: `Added 'set ${name} to 0' at line 1 to declare the missing variable. Change the initial value to match your program's intent.`,
        assumptions: [`${name} should be a number; change the type if needed`],
        limitations: ["The compiler must validate this change"],
      });
    }

    // Rule 2: Return outside function
    if (/[Rr]eturn outside (function|body)/.test(message) || /[Rr]eturn statement is only valid inside a function/.test(message)) {
      return JSON.stringify({
        source,
        explanation: "A `give back` statement was found outside a function body. To fix this, move the `give back` statement inside a `create ... end` function declaration, or remove it if it is not intended to be inside a function.",
        assumptions: [],
        limitations: ["This requires understanding the intended program structure — no automatic repair is possible."],
      });
    }

    // Rule 3: Duplicate declaration
    m = message.match(/[Vv]ariable '([^']+)' is already declared/) ||
        message.match(/[Ff]unction '([^']+)' is already declared/) ||
        message.match(/[Aa]lready declared '([^']+)'/) ||
        message.match(/[Dd]uplicate declaration '([^']+)'/);
    if (m) {
      const name = m[1];
      const lines = source.split("\n");
      let found = 0;
      const fixed = lines.filter((l) => {
        if (/^[\s]*set\s+/.test(l) && new RegExp(`\\bset\\s+${name}\\s+to\\b`).test(l)) {
          found += 1;
          if (found === 2) return false;
        }
        return true;
      });
      if (found >= 2) {
        return JSON.stringify({
          source: fixed.join("\n"),
          explanation: `Removed the second declaration of '${name}'. Only the first declaration is kept; subsequent assignments should use the variable directly without \`set\`.`,
          assumptions: [`The first declaration of '${name}' has the correct initial value`],
          limitations: ["The compiler must validate this change"],
        });
      }
    }
  }

  // Rule 4: SyntaxError — cannot safely auto-fix
  if (category === "SyntaxError") {
    return JSON.stringify({
      source,
      explanation: `A syntax error was found at line ${line}: '${message}'. Syntax errors require manual correction. Check that all keywords are correctly spelled and that each statement is complete and properly formed.`,
      assumptions: [],
      limitations: ["Syntax errors cannot be safely repaired automatically — the intended structure is ambiguous."],
    });
  }

  // Rule 5: RuntimeError division by zero
  if (category === "RuntimeError" && (/[Dd]ivision by zero/.test(message) || /[Dd]ivide.*by.*zero/.test(message))) {
    return JSON.stringify({
      source,
      explanation: `Division by zero was detected at line ${line}. To fix this, add a condition before the division that checks the denominator is not equal to 0 (e.g., \`if denominator is not equal to 0 then\`).`,
      assumptions: [],
      limitations: ["The variable name of the denominator must be determined from the source context."],
    });
  }

  // All other diagnostics
  const suggestionText = suggestion ? ` The compiler suggestion is: ${suggestion}` : "";
  return JSON.stringify({
    source,
    explanation: `No automatic repair rule matches this diagnostic. Review the message at line ${line}: '${message}'.${suggestionText}`,
    assumptions: [],
    limitations: ["Manual review and correction required."],
  });
}

// ---------------------------------------------------------------------------
// Generate task
// ---------------------------------------------------------------------------

function handleGenerate(context) {
  const description = (context.description ?? "").toLowerCase();

  let source;
  let explanationSuffix;

  if (/hello|greeting/.test(description)) {
    source = 'say "Hello, World!"\n';
    explanationSuffix = "This program outputs a greeting message.";
  } else if (/age/.test(description) && (/adult|check/.test(description))) {
    source = [
      "ask age as number",
      "if age is at least 18 then",
      '    say "You are an adult."',
      "otherwise",
      '    say "You are not an adult."',
      "end",
      "",
    ].join("\n");
    explanationSuffix = "This program reads an age and checks whether the person is an adult (18 or older).";
  } else if (/score/.test(description) && (/pass|grade/.test(description))) {
    source = [
      "ask score as number",
      "if score is at least 40 then",
      '    say "Passed"',
      "otherwise",
      '    say "Failed"',
      "end",
      "",
    ].join("\n");
    explanationSuffix = "This program reads a score and reports whether the student passed (40 or above).";
  } else if (/count|loop|repeat/.test(description)) {
    source = [
      "set counter to 1",
      "repeat 5 times",
      "    say counter",
      "    set counter to counter plus 1",
      "end",
      "",
    ].join("\n");
    explanationSuffix = "This program counts from 1 to 5 using a repeat loop.";
  } else if (/add|sum|total/.test(description)) {
    source = [
      "set first to 10",
      "set second to 20",
      "set total to first plus second",
      "say total",
      "",
    ].join("\n");
    explanationSuffix = "This program adds two numbers and displays the result.";
  } else if (/name/.test(description) && /greet/.test(description)) {
    source = [
      "ask name",
      'say "Hello, "',
      "say name",
      "",
    ].join("\n");
    explanationSuffix = "This program asks for a name and greets the user.";
  } else if (/function|create|give back|return/.test(description)) {
    source = [
      "create double using value",
      "    give back value times 2",
      "end",
      "set result to call double using 21",
      "say result",
      "",
    ].join("\n");
    explanationSuffix = "This program defines a one-parameter function, returns a value, calls it, and prints the result.";
  } else {
    source = [
      "set answer to 42",
      "say answer",
      "",
    ].join("\n");
    explanationSuffix = "This is a simple default program that declares a variable and prints its value.";
  }

  return JSON.stringify({
    source,
    explanation: `This is a template-based example. ${explanationSuffix}`,
    assumptions: ["Values are fixed; adjust them to match your specific requirements."],
    limitations: ["This example is a fixed template, not AI-generated. Edit it to match your exact requirements."],
  });
}

// ---------------------------------------------------------------------------
// AST task
// ---------------------------------------------------------------------------

const AST_DESCRIPTIONS = {
  VariableDeclaration: {
    explanation: "A variable declaration. The `set` statement creates a new variable and assigns it an initial value. The name metadata child holds the identifier; the second child is the initializer expression.",
    relevantRule: 'varDecl ::= "set" IDENTIFIER "to" expression NEWLINE',
  },
  PrintStatement: {
    explanation: "An output statement. The `say` keyword evaluates the expression child and prints its value to standard output.",
    relevantRule: 'outputStmt ::= "say" expression NEWLINE',
  },
  IfStatement: {
    explanation: "A conditional statement. The `if...then...otherwise...end` construct evaluates a boolean condition and executes one of two branches.",
    relevantRule: 'ifStmt ::= "if" condition "then" block ( "otherwise" block )? "end" NEWLINE',
  },
  WhileStatement: {
    explanation: "A while loop. `repeat while CONDITION` repeats the body block as long as the condition remains true.",
    relevantRule: 'whileStmt ::= "repeat" "while" condition block "end" NEWLINE',
  },
  RepeatTimesStatement: {
    explanation: "A count-controlled loop. `repeat N times` executes the body block exactly N times.",
    relevantRule: 'repeatStmt ::= "repeat" expression "times" block "end" NEWLINE',
  },
  InputStatement: {
    explanation: "An input statement. `ask NAME` reads a line of text; `ask NAME as number` reads an integer; `ask NAME as decimal` reads a floating-point value.",
    relevantRule: 'inputStmt ::= "ask" IDENTIFIER ( "as" inputType )? NEWLINE',
  },
  FunctionDeclaration: {
    explanation: "A function declaration. `create NAME using PARAM ... end` defines a reusable named block with one optional parameter.",
    relevantRule: 'funcDecl ::= "create" IDENTIFIER ( "using" IDENTIFIER )? block "end" NEWLINE',
  },
  ReturnStatement: {
    explanation: "A return statement. `give back EXPRESSION` exits the enclosing function and returns the expression's value.",
    relevantRule: 'returnStmt ::= "give" "back" expression NEWLINE',
  },
  FunctionCall: {
    explanation: "A function call node. Its name is stored in metadata and its optional argument, when present, is its child expression.",
    relevantRule: 'call ::= "call" IDENTIFIER ( "using" expression )?',
  },
  BinaryExpression: {
    explanation: "A binary arithmetic expression. The operator metadata child names the operation; the two value children are the left and right operands.",
    relevantRule: 'expression ::= expression operator expression | atom',
  },
  Literal: {
    explanation: "A literal value node. The metadata children hold the value and kind (number, decimal, text, boolean).",
    relevantRule: 'literal ::= NUMBER | DECIMAL | STRING | "yes" | "no"',
  },
  VariableReference: {
    explanation: "An identifier reference. This node refers to a previously declared variable by name.",
    relevantRule: 'identifier ::= IDENTIFIER',
  },
  Comparison: {
    explanation: "A comparison expression. Produces a boolean from two operand expressions using the named comparison operator.",
    relevantRule: 'comparison ::= expression compareOp expression',
  },
  LogicalExpression: {
    explanation: "A logical expression. Its operator metadata names the logical operation and its two child nodes are the operands.",
    relevantRule: 'condition ::= condition ("and" | "or") condition',
  },
  UnaryExpression: {
    explanation: "A unary expression. The operator is stored in node metadata and the operand is the child expression.",
    relevantRule: 'condition ::= "not" condition',
  },
};

function handleAst(context) {
  const { node } = context;
  if (!node || typeof node !== "object") {
    return JSON.stringify({
      explanation: "No AST node context was provided.",
      relevantRule: "N/A",
    });
  }

  const { type = "Unknown", line = 0, column = 0 } = node;
  const entry = AST_DESCRIPTIONS[type];
  if (entry) {
    return JSON.stringify({
      explanation: entry.explanation,
      relevantRule: entry.relevantRule,
    });
  }

  return JSON.stringify({
    explanation: `An AST node of type ${type} at line ${line}, column ${column}.`,
    relevantRule: `${type} ::= (see grammar documentation)`,
  });
}

// ---------------------------------------------------------------------------
// Trace task
// ---------------------------------------------------------------------------

function classifyTraceEvent(event) {
  let m;

  m = event.match(/^declare (\w+) = (.+)$/);
  if (m) return `Line declares variable ${m[1]} and assigns it ${m[2]}.`;

  m = event.match(/^assign (\w+) = (.+)$/) || event.match(/^set (\w+) to (.+)$/);
  if (m) return `Variable ${m[1]} is updated to ${m[2]}.`;

  m = event.match(/^say (.+)$/) || event.match(/^output: (.+)$/);
  if (m) return `The program outputs: ${m[1]}.`;

  m = event.match(/^if (.+): (true|false)$/) || event.match(/^branch (.+) -> (true|false)$/);
  if (m) return `The condition was evaluated and found to be ${m[2]}.`;

  m = event.match(/^call (\w+)$/) || event.match(/^call (\w+)\(.*\)$/);
  if (m) return `Function ${m[1]} is invoked.`;

  m = event.match(/^return (.+)$/) || event.match(/^give back (.+)$/);
  if (m) return `The function returns value ${m[1]}.`;

  m = event.match(/^loop iteration (\d+)$/) || event.match(/^repeat (.+)$/);
  if (m) return `Loop iteration ${event}.`;

  m = event.match(/^ask (\w+)$/);
  if (m) return `The interpreter pauses at \`ask ${m[1]}\`, waiting for user input.`;

  return event;
}

function handleTrace(context) {
  const { events } = context;
  if (!Array.isArray(events) || events.length === 0) {
    return JSON.stringify({
      explanation: "No trace events were provided.",
      observations: [],
    });
  }

  const observations = events.map((event) => classifyTraceEvent(String(event)));
  const count = events.length;
  const summary = observations.join(" ");

  return JSON.stringify({
    explanation: `The trace contains ${count} event(s): ${summary}`,
    observations,
  });
}

// ---------------------------------------------------------------------------
// createOfflineAssistant — public factory
// ---------------------------------------------------------------------------

export function createOfflineAssistant() {
  return {
    async generate(task, context) {
      switch (task) {
        case "diagnostic": return handleDiagnostic(context);
        case "repair":     return handleRepair(context);
        case "generate":   return handleGenerate(context);
        case "ast":        return handleAst(context);
        case "trace":      return handleTrace(context);
        default:           throw new Error("Unsupported assistant task.");
      }
    },
  };
}

// ---------------------------------------------------------------------------
// parseStructuredResponse — moved from ai.js
// ---------------------------------------------------------------------------

export function parseStructuredResponse(response, task) {
  if (typeof response !== "string" || Buffer.byteLength(response, "utf8") > 64 * 1024) {
    throw new Error("The offline assistant returned an empty or oversized response.");
  }
  let value;
  try {
    value = JSON.parse(response);
  } catch {
    throw new Error("The offline assistant returned invalid structured JSON.");
  }
  const string = (candidate, limit = 4000) =>
    typeof candidate === "string" && candidate.trim().length > 0 && candidate.length <= limit;
  const stringArray = (candidate) =>
    Array.isArray(candidate) && candidate.length <= 20 && candidate.every((item) => string(item, 500));

  if (!value || typeof value !== "object" || Array.isArray(value)) {
    throw new Error("The offline assistant returned an invalid response shape.");
  }
  if (task === "generate" || task === "repair") {
    if (!string(value.source, 64 * 1024) || !string(value.explanation) ||
        !stringArray(value.assumptions) || !stringArray(value.limitations)) {
      throw new Error("The offline assistant returned an invalid code response.");
    }
  } else if (task === "diagnostic" || task === "ast") {
    if (!string(value.explanation) || !string(value.relevantRule)) {
      throw new Error("The offline assistant returned an invalid explanation.");
    }
  } else if (task === "trace") {
    if (!string(value.explanation) || !stringArray(value.observations)) {
      throw new Error("The offline assistant returned an invalid trace explanation.");
    }
  } else {
    throw new Error("Unsupported assistant task.");
  }
  return value;
}
