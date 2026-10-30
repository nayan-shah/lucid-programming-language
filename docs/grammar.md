# Lucid Language Grammar

This document defines the formal EBNF grammar for the Lucid programming language.

## File Extension

`.lc`

## Types

| Lucid Type | Description       | Literal Examples       |
|------------|-------------------|------------------------|
| `number`   | Integer           | `0`, `42`, `100`       |
| `decimal`  | Floating-point    | `3.14`, `0.5`, `98.6`  |
| `text`     | String            | `"hello"`, `"world"`   |
| `boolean`  | Boolean           | `yes`, `no`            |

## EBNF Grammar

```ebnf
program      ::= statement*

statement    ::= varDecl
               | outputStmt
               | ifStmt
               | whileStmt
               | repeatTimesStmt
               | inputStmt
               | funcDecl
               | returnStmt
               | callStmt

varDecl      ::= "set" IDENTIFIER "to" expression NEWLINE

outputStmt   ::= "say" expression NEWLINE

ifStmt       ::= "if" condition "then" NEWLINE
                  block
                  ("otherwise" NEWLINE block)?
                  "end" NEWLINE

whileStmt    ::= "repeat" "while" condition NEWLINE
                  block
                  "end" NEWLINE

repeatTimesStmt ::= "repeat" factor "times" NEWLINE
                     block
                     "end" NEWLINE

inputStmt    ::= "ask" IDENTIFIER ("as" inputType)? NEWLINE
inputType    ::= "text" | "number" | "decimal"

funcDecl     ::= "create" IDENTIFIER ("using" IDENTIFIER ( "," IDENTIFIER )* )? NEWLINE
                  block
                  "end" NEWLINE

returnStmt   ::= "give back" expression NEWLINE

callStmt     ::= "call" IDENTIFIER ("using" expression ( "," expression )* )? NEWLINE

block        ::= statement*

condition    ::= orCondition

orCondition  ::= andCondition ("or" andCondition)*

andCondition ::= notCondition ("and" notCondition)*

notCondition ::= "not" notCondition | comparison

comparison   ::= expression compOp expression

compOp       ::= "is equal to" | "is not equal to"
               | "is greater than" | "is less than"
               | "is at least" | "is at most"

expression   ::= term (("plus" | "minus") term)*

term         ::= factor (("times" | "divided_by") factor)*

factor       ::= NUMBER | DECIMAL | STRING
               | "yes" | "no"
               | IDENTIFIER
               | callExpr
               | "(" expression ")"

callExpr     ::= "call" IDENTIFIER ("using" expression ( "," expression )* )?
```

## Operator Precedence (low to high)

| Precedence | Operators                          | Associativity |
|------------|------------------------------------|---------------|
| 1 (lowest) | `or`                               | Left          |
| 2          | `and`                              | Left          |
| 3          | `not`                              | Right (unary) |
| 4          | Comparison (`is_equal_to`, etc.)   | Non-assoc     |
| 5          | `plus`, `minus`                    | Left          |
| 6 (highest)| `times`, `divided_by`              | Left          |

## Comments

Comments begin with `note:` and extend to the end of the line.
They are discarded by the lexer and do not appear in the token stream.

```
note: this is a comment
set x to 5  note: inline comments are NOT supported (must be on own line)
```

## Multi-Word Phrase Operators

The following phrases are lexed as **single tokens** via a greedy
longest-match phrase table:

| Source Phrase        | Token Type         | Words |
|----------------------|--------------------|-------|
| `give back`          | `GIVE_BACK`        | 2     |
| `is not equal to`    | `IS_NOT_EQUAL_TO`  | 4     |
| `is equal to`        | `IS_EQUAL_TO`      | 3     |
| `is greater than`    | `IS_GREATER_THAN`  | 3     |
| `is less than`       | `IS_LESS_THAN`     | 3     |
| `is at least`        | `IS_AT_LEAST`      | 3     |
| `is at most`         | `IS_AT_MOST`       | 3     |
| `divided by`         | `DIVIDED_BY`       | 2     |

## Phase-2 Implemented Features

| Feature              | Syntax Example                                     |
|----------------------|----------------------------------------------------|
| While loop           | `repeat while x is greater than 0`                |
| Count loop           | `repeat 5 times`                                   |
| User input           | `ask name` -- reads text; `ask age as number` reads an integer; `ask price as decimal` reads a decimal |
| Function declaration | `create greet using name` ... `end`; multiple names use commas |
| Return value         | `give back result`                                 |
| Function call (stmt) | `call greet using "Alice"`; multiple expressions use commas |
| Function call (expr) | `set x to call compute using 10, 20`              |

## Deferred Features (Phase 3)

- Recursive or mutually recursive return-type inference when parameter-dependent types cannot be established
- IR generation / optimizer
- AST-to-English explainer

Typed input is converted by the interpreter at runtime. Invalid numeric input
produces a runtime diagnostic; `number` accepts whole values representable by
the compiler's integer range, and `decimal` accepts finite decimal values.

Function declarations support zero or more parameters after `using`, for example
`create add using left, right`. Calls pass the same number of comma-separated
expressions: `call add using 2, 3`. The semantic analyzer reports argument-count
errors. It infers a return type from consistent known return expressions;
parameter-dependent and recursive cases remain `unknown`, while incompatible
return paths are errors.
