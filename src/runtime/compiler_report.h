#ifndef COMPILER_REPORT_H
#define COMPILER_REPORT_H

#include <memory>
#include <string>
#include <vector>

#include "../ast/nodes.h"
#include "../lexer/lexer.h"
#include "../lexer/token.h"
#include "../parser/parser.h"
#include "../semantic/analyzer.h"
#include "interpreter.h"

std::string jsonEscape(const std::string& text);
std::string serializeAstToJson(std::shared_ptr<Program> program);
std::string buildCompilerReportJson(
    const std::string& filename,
    const std::vector<Token>& tokens,
    const std::vector<LexerError>& lexErrors,
    const std::vector<ParseError>& parseErrors,
    const std::vector<SemanticError>& semanticErrors,
    std::shared_ptr<Program> program,
    const std::vector<SymbolEntry>& symbols,
    const Interpreter::ExecutionResult* executionResult = nullptr,
    const std::string& source = "");

#endif
