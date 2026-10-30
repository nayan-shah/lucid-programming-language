#include "compiler_report.h"
#include "../errors/rejection_explainer.h"

#include <map>
#include <sstream>
#include <utility>

static std::string toJsonValueString(const std::string& value) {
    return "\"" + jsonEscape(value) + "\"";
}

static std::string sourceLineAt(const std::string& source, int requestedLine) {
    if (requestedLine < 1) return "";
    std::istringstream lines(source);
    std::string line;
    for (int current = 1; std::getline(lines, line); ++current) {
        if (current == requestedLine) {
            if (!line.empty() && line[line.size() - 1] == '\r') line.erase(line.size() - 1);
            return line;
        }
    }
    return "";
}

static void appendDiagnosticSource(std::ostringstream& out, const std::string& source, int line, int column) {
    out << ",\"sourceLine\":" << toJsonValueString(sourceLineAt(source, line));
    std::string sourceLine = sourceLineAt(source, line);
    RejectionExplainer explainer;
    const std::vector<ErrorObject> suggestions = explainer.explainLine(sourceLine, line);
    for (const auto& suggestion : suggestions) {
        if (suggestion.column == column && !suggestion.suggestion.empty()) {
            out << ",\"suggestion\":" << toJsonValueString(suggestion.suggestion);
            break;
        }
    }
}

std::string jsonEscape(const std::string& text) {
    std::ostringstream escaped;
    for (char ch : text) {
        switch (ch) {
            case '\\': escaped << "\\\\"; break;
            case '"': escaped << "\\\""; break;
            case '\n': escaped << "\\n"; break;
            case '\r': escaped << "\\r"; break;
            case '\t': escaped << "\\t"; break;
            case '\b': escaped << "\\b"; break;
            case '\f': escaped << "\\f"; break;
            default:
                if (static_cast<unsigned char>(ch) < 0x20) {
                    const char hex[] = "0123456789abcdef";
                    escaped << "\\u00" << hex[(static_cast<unsigned char>(ch) >> 4) & 0x0f]
                            << hex[static_cast<unsigned char>(ch) & 0x0f];
                } else {
                    escaped << ch;
                }
                break;
        }
    }
    return escaped.str();
}

static void appendAstNode(std::ostringstream& out, ASTNodePtr node, int& nextId) {
    if (!node) {
        out << "null";
        return;
    }

    out << "{\"id\":" << nextId++
        << ",\"type\":" << toJsonValueString(node->nodeType())
        << ",\"line\":" << node->line
        << ",\"column\":" << node->column
        << ",\"children\":[";

    bool firstChild = true;
    auto appendChild = [&](ASTNodePtr child) {
        if (!child) return;
        if (!firstChild) out << ",";
        appendAstNode(out, child, nextId);
        firstChild = false;
    };

    if (auto vd = std::dynamic_pointer_cast<VariableDeclaration>(node)) {
        out << "{\"name\":" << toJsonValueString(vd->name) << "}";
        firstChild = false;
        appendChild(vd->expression);
    } else if (auto ps = std::dynamic_pointer_cast<PrintStatement>(node)) {
        appendChild(ps->expression);
    } else if (auto ifStmt = std::dynamic_pointer_cast<IfStatement>(node)) {
        appendChild(ifStmt->condition);
        for (const auto& stmt : ifStmt->thenBlock) appendChild(stmt);
        for (const auto& stmt : ifStmt->otherwiseBlock) appendChild(stmt);
    } else if (auto whileStmt = std::dynamic_pointer_cast<WhileStatement>(node)) {
        appendChild(whileStmt->condition);
        for (const auto& stmt : whileStmt->body) appendChild(stmt);
    } else if (auto repeatStmt = std::dynamic_pointer_cast<RepeatTimesStatement>(node)) {
        appendChild(repeatStmt->count);
        for (const auto& stmt : repeatStmt->body) appendChild(stmt);
    } else if (auto inputStmt = std::dynamic_pointer_cast<InputStatement>(node)) {
        out << "{\"name\":" << toJsonValueString(inputStmt->varName)
            << ",\"inputType\":" << toJsonValueString(inputStmt->inputType) << "}";
        firstChild = false;
    } else if (auto funcDecl = std::dynamic_pointer_cast<FunctionDeclaration>(node)) {
        out << "{\"name\":" << toJsonValueString(funcDecl->name) << ",\"param\":" << toJsonValueString(funcDecl->param) << ",\"params\":[";
        for (size_t i = 0; i < funcDecl->params.size(); ++i) { if (i) out << ","; out << toJsonValueString(funcDecl->params[i]); }
        out << "]}";
        firstChild = false;
        for (const auto& stmt : funcDecl->body) appendChild(stmt);
    } else if (auto ret = std::dynamic_pointer_cast<ReturnStatement>(node)) {
        appendChild(ret->expression);
    } else if (auto call = std::dynamic_pointer_cast<FunctionCall>(node)) {
        out << "{\"name\":" << toJsonValueString(call->name) << "}";
        firstChild = false;
        for (const auto& arg : call->arguments) appendChild(arg);
    } else if (auto lit = std::dynamic_pointer_cast<Literal>(node)) {
        out << "{\"value\":" << toJsonValueString(lit->value) << ",\"kind\":" << toJsonValueString(lit->valueType) << "}";
        firstChild = false;
    } else if (auto vr = std::dynamic_pointer_cast<VariableReference>(node)) {
        out << "{\"name\":" << toJsonValueString(vr->name) << "}";
        firstChild = false;
    } else if (auto be = std::dynamic_pointer_cast<BinaryExpression>(node)) {
        out << "{\"op\":" << toJsonValueString(be->op) << "}";
        firstChild = false;
        appendChild(be->left);
        appendChild(be->right);
    } else if (auto cmp = std::dynamic_pointer_cast<Comparison>(node)) {
        out << "{\"op\":" << toJsonValueString(cmp->op) << "}";
        firstChild = false;
        appendChild(cmp->left);
        appendChild(cmp->right);
    } else if (auto le = std::dynamic_pointer_cast<LogicalExpression>(node)) {
        out << "{\"op\":" << toJsonValueString(le->op) << "}";
        firstChild = false;
        appendChild(le->left);
        appendChild(le->right);
    } else if (auto ue = std::dynamic_pointer_cast<UnaryExpression>(node)) {
        out << "{\"op\":" << toJsonValueString(ue->op) << "}";
        firstChild = false;
        appendChild(ue->operand);
    } else if (auto programNode = std::dynamic_pointer_cast<Program>(node)) {
        for (const auto& stmt : programNode->statements) appendChild(stmt);
    }

    out << "]}";
}

struct AstMetrics {
    size_t nodeCount;
    size_t maxDepth;
    size_t maxNesting;
    std::map<std::string, size_t> statementTypes;

    AstMetrics() : nodeCount(0), maxDepth(0), maxNesting(0) {}
};

static void measureNode(ASTNodePtr node, size_t depth, size_t nesting, bool statementPosition, AstMetrics& metrics) {
    if (!node) return;
    ++metrics.nodeCount;
    if (depth > metrics.maxDepth) metrics.maxDepth = depth;
    if (nesting > metrics.maxNesting) metrics.maxNesting = nesting;

    std::string type = node->nodeType();
    if (statementPosition && (type == "VariableDeclaration" || type == "PrintStatement" ||
        type == "IfStatement" || type == "WhileStatement" ||
        type == "RepeatTimesStatement" || type == "InputStatement" ||
        type == "FunctionDeclaration" || type == "ReturnStatement" ||
        type == "FunctionCall")) {
        ++metrics.statementTypes[type];
    }

    size_t childNesting = nesting;
    if (type == "IfStatement" || type == "WhileStatement" ||
        type == "RepeatTimesStatement" || type == "FunctionDeclaration") {
        ++childNesting;
    }
    auto measureExpression = [&](ASTNodePtr child) { measureNode(child, depth + 1, childNesting, false, metrics); };
    auto measureStatement = [&](ASTNodePtr child) { measureNode(child, depth + 1, childNesting, true, metrics); };
    if (auto vd = std::dynamic_pointer_cast<VariableDeclaration>(node)) measureExpression(vd->expression);
    else if (auto ps = std::dynamic_pointer_cast<PrintStatement>(node)) measureExpression(ps->expression);
    else if (auto ifStmt = std::dynamic_pointer_cast<IfStatement>(node)) {
        measureExpression(ifStmt->condition);
        for (const auto& stmt : ifStmt->thenBlock) measureStatement(stmt);
        for (const auto& stmt : ifStmt->otherwiseBlock) measureStatement(stmt);
    } else if (auto whileStmt = std::dynamic_pointer_cast<WhileStatement>(node)) {
        measureExpression(whileStmt->condition);
        for (const auto& stmt : whileStmt->body) measureStatement(stmt);
    } else if (auto repeatStmt = std::dynamic_pointer_cast<RepeatTimesStatement>(node)) {
        measureExpression(repeatStmt->count);
        for (const auto& stmt : repeatStmt->body) measureStatement(stmt);
    } else if (auto funcDecl = std::dynamic_pointer_cast<FunctionDeclaration>(node)) {
        for (const auto& stmt : funcDecl->body) measureStatement(stmt);
    } else if (auto ret = std::dynamic_pointer_cast<ReturnStatement>(node)) measureExpression(ret->expression);
    else if (auto call = std::dynamic_pointer_cast<FunctionCall>(node)) for (const auto& arg : call->arguments) measureExpression(arg);
    else if (auto be = std::dynamic_pointer_cast<BinaryExpression>(node)) {
        measureExpression(be->left);
        measureExpression(be->right);
    } else if (auto cmp = std::dynamic_pointer_cast<Comparison>(node)) {
        measureExpression(cmp->left);
        measureExpression(cmp->right);
    } else if (auto logical = std::dynamic_pointer_cast<LogicalExpression>(node)) {
        measureExpression(logical->left);
        measureExpression(logical->right);
    } else if (auto unary = std::dynamic_pointer_cast<UnaryExpression>(node)) measureExpression(unary->operand);
}

static AstMetrics measureProgram(std::shared_ptr<Program> program) {
    AstMetrics metrics;
    if (!program) return metrics;
    for (const auto& stmt : program->statements) measureNode(stmt, 1, 0, true, metrics);
    return metrics;
}

std::string serializeAstToJson(std::shared_ptr<Program> program) {
    if (!program) return "[]";
    std::ostringstream out;
    out << "[";
    int nextId = 1;
    for (size_t i = 0; i < program->statements.size(); ++i) {
        if (i > 0) out << ",";
        appendAstNode(out, program->statements[i], nextId);
    }
    out << "]";
    return out.str();
}

std::string buildCompilerReportJson(
    const std::string& filename,
    const std::vector<Token>& tokens,
    const std::vector<LexerError>& lexErrors,
    const std::vector<ParseError>& parseErrors,
    const std::vector<SemanticError>& semanticErrors,
    std::shared_ptr<Program> program,
    const std::vector<SymbolEntry>& symbols,
    const Interpreter::ExecutionResult* executionResult,
    const std::string& source) {

    std::ostringstream out;
    bool frontendFailed = !lexErrors.empty() || !parseErrors.empty();
    bool semanticWasRun = !frontendFailed;
    bool compilationFailed = frontendFailed || !semanticErrors.empty();
    bool runtimeFailed = executionResult != nullptr && !executionResult->ok;
    AstMetrics astMetrics = frontendFailed ? AstMetrics() : measureProgram(program);
    out << "{\n";
    out << "  \"schemaVersion\": \"1.1\",\n";
    out << "  \"requestId\": " << toJsonValueString(filename) << ",\n";
    out << "  \"status\": " << toJsonValueString((compilationFailed || runtimeFailed) ? "error" : "success") << ",\n";
    out << "  \"lexer\": {\"status\": " << toJsonValueString(lexErrors.empty() ? "success" : "failed")
        << ", \"errorCount\":" << lexErrors.size() << "},\n";
    out << "  \"parser\": {\"status\": " << toJsonValueString(frontendFailed ? "failed" : "success")
        << ", \"errorCount\":" << parseErrors.size() << "},\n";
    out << "  \"ast\": {\"status\": " << toJsonValueString(frontendFailed ? "unavailable" : "available")
        << ", \"nodes\":" << (frontendFailed ? "[]" : serializeAstToJson(program)) << "},\n";
    out << "  \"semantic\": {\"status\": "
        << toJsonValueString(!semanticWasRun ? "skipped" : semanticErrors.empty() ? "success" : "failed")
        << ", \"errorCount\":" << semanticErrors.size() << "},\n";
    out << "  \"execution\": {\"status\": "
        << toJsonValueString(executionResult ? (executionResult->ok ? "success" : "failed") : compilationFailed ? "skipped" : "not_run");
    if (executionResult) {
        out << ", \"output\": [";
        for (size_t i = 0; i < executionResult->output.size(); ++i) {
            if (i > 0) out << ", ";
            out << toJsonValueString(executionResult->output[i]);
        }
        out << "], \"trace\": [";
        for (size_t i = 0; i < executionResult->events.size(); ++i) {
            if (i > 0) out << ", ";
            out << toJsonValueString(executionResult->events[i]);
        }
        out << "]";
        if (!executionResult->ok) {
            out << ", \"error\": " << toJsonValueString(executionResult->error)
                << ", \"line\": " << executionResult->line
                << ", \"column\": " << executionResult->column;
        }
    }
    out << "},\n";
    out << "  \"tokens\": [";
    for (size_t i = 0; i < tokens.size(); ++i) {
        if (i > 0) out << ", ";
        out << "{\"type\":" << toJsonValueString(tokenTypeName(tokens[i].type))
            << ",\"value\":" << toJsonValueString(tokens[i].value)
            << ",\"line\":" << tokens[i].line
            << ",\"column\":" << tokens[i].column << "}";
    }
    out << "],\n";
    out << "  \"symbols\": [";
    for (size_t i = 0; i < symbols.size(); ++i) {
        if (i > 0) out << ", ";
        out << "{\"name\":" << toJsonValueString(symbols[i].name)
            << ",\"type\":" << toJsonValueString(symbols[i].type)
            << ",\"scopeId\":" << symbols[i].scopeId
            << ",\"declaredAtLine\":" << symbols[i].declaredAtLine << "}";
    }
    out << "],\n";
    out << "  \"diagnostics\": [";
    size_t diagIndex = 0;
    for (const auto& err : lexErrors) {
        if (diagIndex++ > 0) out << ", ";
        out << "{\"category\":\"LexicalError\",\"message\":" << toJsonValueString(err.message)
            << ",\"line\":" << err.line << ",\"column\":" << err.column;
        appendDiagnosticSource(out, source, err.line, err.column);
        out << "}";
    }
    for (const auto& err : parseErrors) {
        if (diagIndex++ > 0) out << ", ";
        out << "{\"category\":\"SyntaxError\",\"message\":" << toJsonValueString(err.message)
            << ",\"line\":" << err.line << ",\"column\":" << err.column;
        appendDiagnosticSource(out, source, err.line, err.column);
        out << "}";
    }
    for (const auto& err : semanticErrors) {
        if (diagIndex++ > 0) out << ", ";
        out << "{\"category\":" << toJsonValueString(err.category)
            << ",\"message\":" << toJsonValueString(err.message)
            << ",\"line\":" << err.line << ",\"column\":" << err.column;
        appendDiagnosticSource(out, source, err.line, err.column);
        out << "}";
    }
    if (runtimeFailed) {
        if (diagIndex++ > 0) out << ", ";
        out << "{\"category\":\"RuntimeError\",\"message\":" << toJsonValueString(executionResult->error)
            << ",\"line\":" << executionResult->line << ",\"column\":" << executionResult->column;
        appendDiagnosticSource(out, source, executionResult->line, executionResult->column);
        out << "}";
    }
    out << "],\n";
    out << "  \"metrics\": {\"tokenCount\":" << tokens.size()
        << ", \"astNodeCount\":" << astMetrics.nodeCount
        << ", \"astDepth\":" << astMetrics.maxDepth
        << ", \"maximumNestingDepth\":" << astMetrics.maxNesting
        << ", \"statementTypes\": {";
    size_t statementTypeIndex = 0;
    for (const auto& entry : astMetrics.statementTypes) {
        if (statementTypeIndex++ > 0) out << ", ";
        out << toJsonValueString(entry.first) << ":" << entry.second;
    }
    out << "}, \"stageTimings\": null},\n";
    out << "  \"sourceLength\": " << source.size() << "\n";
    out << "}\n";
    return out.str();
}
