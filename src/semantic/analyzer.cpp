#include "analyzer.h"
#include <sstream>

const std::set<std::string> SemanticAnalyzer::numericTypes = {"number", "decimal"};

SemanticAnalyzer::SemanticAnalyzer() : functionDepth(0) {}

std::vector<SemanticError> SemanticAnalyzer::analyze(std::shared_ptr<Program> program) {
    errs.clear();
    functions.clear();
    functionReturnTypes.clear();
    functionDepth = 0;
    symbolTable = SymbolTable();
    for (const auto& stmt : program->statements) {
        if (auto function = std::dynamic_pointer_cast<FunctionDeclaration>(stmt)) {
            functions[function->name] = function;
            functionReturnTypes[function->name] = "unknown";
        }
    }
    for (size_t pass = 0; pass <= functions.size(); ++pass) {
        errs.clear();
        symbolTable = SymbolTable();
        functionDepth = 0;
        for (auto& stmt : program->statements) visit(stmt);
    }
    errs.clear();
    symbolTable = SymbolTable();
    functionDepth = 0;
    for (auto& stmt : program->statements) visit(stmt);
    return errs;
}

std::string SemanticAnalyzer::visit(ASTNodePtr node) {
    if (auto vd = std::dynamic_pointer_cast<VariableDeclaration>(node)) {
        visitVarDecl(vd);
        return "";
    } else if (auto ps = std::dynamic_pointer_cast<PrintStatement>(node)) {
        visitPrintStmt(ps);
        return "";
    } else if (auto is = std::dynamic_pointer_cast<IfStatement>(node)) {
        visitIfStmt(is);
        return "";
    } else if (auto ws = std::dynamic_pointer_cast<WhileStatement>(node)) {
        visitWhileStmt(ws);
        return "";
    } else if (auto rts = std::dynamic_pointer_cast<RepeatTimesStatement>(node)) {
        visitRepeatTimesStmt(rts);
        return "";
    } else if (auto ins = std::dynamic_pointer_cast<InputStatement>(node)) {
        visitInputStmt(ins);
        return "";
    } else if (auto fd = std::dynamic_pointer_cast<FunctionDeclaration>(node)) {
        visitFunctionDecl(fd);
        return "";
    } else if (auto rs = std::dynamic_pointer_cast<ReturnStatement>(node)) {
        return visitReturnStmt(rs);
    } else if (auto fc = std::dynamic_pointer_cast<FunctionCall>(node)) {
        return visitFunctionCall(fc);
    } else if (auto be = std::dynamic_pointer_cast<BinaryExpression>(node)) {
        return visitBinaryExpr(be);
    } else if (auto cmp = std::dynamic_pointer_cast<Comparison>(node)) {
        return visitComparison(cmp);
    } else if (auto le = std::dynamic_pointer_cast<LogicalExpression>(node)) {
        return visitLogicalExpr(le);
    } else if (auto ue = std::dynamic_pointer_cast<UnaryExpression>(node)) {
        return visitUnaryExpr(ue);
    } else if (auto lit = std::dynamic_pointer_cast<Literal>(node)) {
        return visitLiteral(lit);
    } else if (auto vr = std::dynamic_pointer_cast<VariableReference>(node)) {
        return visitVarRef(vr);
    }
    return "";
}

void SemanticAnalyzer::visitVarDecl(std::shared_ptr<VariableDeclaration> node) {
    std::string exprType = visit(node->expression);

    SymbolEntry* existingCurrent = symbolTable.lookupCurrentScope(node->name);
    if (existingCurrent != nullptr) {
        if (!exprType.empty() && existingCurrent->type != exprType) {
            errs.push_back(SemanticError(
                "TypeError",
                "Cannot reassign '" + node->name + "' from type '" +
                existingCurrent->type + "' to '" + exprType + "'",
                node->line, node->column));
        } else {
            std::ostringstream ss;
            ss << "Variable '" << node->name << "' is already declared in this scope (line "
               << existingCurrent->declaredAtLine << ")";
            errs.push_back(SemanticError("SemanticError", ss.str(), node->line, node->column));
        }
        return;
    }

    SymbolEntry* existingOuter = symbolTable.lookup(node->name);
    if (existingOuter != nullptr) {
        if (!exprType.empty() && existingOuter->type != exprType) {
            errs.push_back(SemanticError(
                "TypeError",
                "Cannot reassign '" + node->name + "' from type '" +
                existingOuter->type + "' to '" + exprType + "'",
                node->line, node->column));
            return;
        }
        symbolTable.declare(node->name, existingOuter->type, node->line);
        return;
    }

    if (!exprType.empty()) {
        symbolTable.declare(node->name, exprType, node->line);
    } else {
        symbolTable.declare(node->name, "unknown", node->line);
    }
}

void SemanticAnalyzer::visitPrintStmt(std::shared_ptr<PrintStatement> node) {
    visit(node->expression);
}

void SemanticAnalyzer::visitIfStmt(std::shared_ptr<IfStatement> node) {
    const std::string conditionType = visit(node->condition);
    if (!conditionType.empty() && conditionType != "unknown" && conditionType != "boolean") {
        errs.push_back(SemanticError("TypeError", "If condition must be boolean, got '" + conditionType + "'",
                                     node->line, node->column));
    }

    symbolTable.pushScope();
    for (auto& stmt : node->thenBlock) {
        visit(stmt);
    }
    symbolTable.popScope();

    if (node->hasOtherwise) {
        symbolTable.pushScope();
        for (auto& stmt : node->otherwiseBlock) {
            visit(stmt);
        }
        symbolTable.popScope();
    }
}

std::string SemanticAnalyzer::visitBinaryExpr(std::shared_ptr<BinaryExpression> node) {
    std::string leftType = visit(node->left);
    std::string rightType = visit(node->right);

    // 'unknown' means the type will be resolved at runtime (e.g. function
    // parameters, call results) — skip static checking for those operands.
    bool leftOk  = leftType.empty()  || leftType == "unknown" ||
                   numericTypes.find(leftType)  != numericTypes.end();
    bool rightOk = rightType.empty() || rightType == "unknown" ||
                   numericTypes.find(rightType) != numericTypes.end();

    if (!leftOk) {
        errs.push_back(SemanticError(
            "TypeError",
            "Arithmetic operator '" + node->op + "' requires numeric operands, got '" + leftType + "' on the left",
            node->line, node->column));
        return "";
    }

    if (!rightOk) {
        errs.push_back(SemanticError(
            "TypeError",
            "Arithmetic operator '" + node->op + "' requires numeric operands, got '" + rightType + "' on the right",
            node->line, node->column));
        return "";
    }

    if (leftType == "decimal" || rightType == "decimal") {
        return "decimal";
    }
    if (leftType == "unknown" || rightType == "unknown") {
        return "unknown";
    }
    return "number";
}

std::string SemanticAnalyzer::visitComparison(std::shared_ptr<Comparison> node) {
    std::string leftType = visit(node->left);
    std::string rightType = visit(node->right);

    // Skip type-checking if either side is unknown (function param / call result)
    if (!leftType.empty() && !rightType.empty() &&
        leftType != "unknown" && rightType != "unknown") {
        bool leftNumeric  = numericTypes.find(leftType)  != numericTypes.end();
        bool rightNumeric = numericTypes.find(rightType) != numericTypes.end();
        if (leftNumeric && rightNumeric) {
            // ok — mixing number/decimal is allowed
        } else if (leftType != rightType) {
            errs.push_back(SemanticError(
                "TypeError",
                "Cannot compare '" + leftType + "' with '" + rightType +
                "' using '" + node->op + "'",
                node->line, node->column));
        } else if (!leftNumeric && node->op != "is_equal_to" && node->op != "is_not_equal_to") {
            errs.push_back(SemanticError(
                "TypeError",
                "Comparison operator '" + node->op + "' requires numeric operands",
                node->line, node->column));
        }
    }

    return "boolean";
}

std::string SemanticAnalyzer::visitLogicalExpr(std::shared_ptr<LogicalExpression> node) {
    const std::string leftType = visit(node->left);
    const std::string rightType = visit(node->right);
    if (!leftType.empty() && leftType != "unknown" && leftType != "boolean") {
        errs.push_back(SemanticError("TypeError", "Logical operator '" + node->op + "' requires boolean operands",
                                     node->line, node->column));
    }
    if (!rightType.empty() && rightType != "unknown" && rightType != "boolean") {
        errs.push_back(SemanticError("TypeError", "Logical operator '" + node->op + "' requires boolean operands",
                                     node->line, node->column));
    }
    return "boolean";
}

std::string SemanticAnalyzer::visitUnaryExpr(std::shared_ptr<UnaryExpression> node) {
    const std::string operandType = visit(node->operand);
    if (!operandType.empty() && operandType != "unknown" && operandType != "boolean") {
        errs.push_back(SemanticError("TypeError", "Unary 'not' requires a boolean operand, got '" + operandType + "'",
                                     node->line, node->column));
    }
    return "boolean";
}

std::string SemanticAnalyzer::visitLiteral(std::shared_ptr<Literal> node) {
    return node->valueType;
}

std::string SemanticAnalyzer::visitVarRef(std::shared_ptr<VariableReference> node) {
    SymbolEntry* entry = symbolTable.lookup(node->name);
    if (entry == nullptr) {
        errs.push_back(SemanticError(
            "SemanticError",
            "Undefined variable '" + node->name + "'",
            node->line, node->column));
        return "";
    }
    return entry->type;
}

// ── Phase-2 semantic visitors ─────────────────────────────────────────────────

void SemanticAnalyzer::visitWhileStmt(std::shared_ptr<WhileStatement> node) {
    const std::string conditionType = visit(node->condition);
    if (!conditionType.empty() && conditionType != "unknown" && conditionType != "boolean") {
        errs.push_back(SemanticError("TypeError", "While condition must be boolean, got '" + conditionType + "'",
                                     node->line, node->column));
    }
    symbolTable.pushScope();
    for (auto& stmt : node->body) visit(stmt);
    symbolTable.popScope();
}

void SemanticAnalyzer::visitRepeatTimesStmt(std::shared_ptr<RepeatTimesStatement> node) {
    std::string countType = visit(node->count);
    if (!countType.empty() && countType != "number") {
        errs.push_back(SemanticError(
            "TypeError",
            "'repeat ... times' requires a whole number count, got '" + countType + "'",
            node->line, node->column));
    }
    symbolTable.pushScope();
    for (auto& stmt : node->body) visit(stmt);
    symbolTable.popScope();
}

void SemanticAnalyzer::visitInputStmt(std::shared_ptr<InputStatement> node) {
    const std::string& inputType = node->inputType;
    SymbolEntry* existing = symbolTable.lookupCurrentScope(node->varName);
    if (existing != nullptr) {
        if (existing->type != inputType) {
            errs.push_back(SemanticError(
                "TypeError",
                "Cannot change type of '" + node->varName + "' from '" +
                existing->type + "' to '" + inputType + "' via 'ask'",
                node->line, node->column));
        }
        // same type — silent re-assignment, no error
    } else {
        symbolTable.declare(node->varName, inputType, node->line);
    }
}

void SemanticAnalyzer::visitFunctionDecl(std::shared_ptr<FunctionDeclaration> node) {
    // Check for duplicate function name in current scope
    SymbolEntry* existing = symbolTable.lookupCurrentScope(node->name);
    if (existing != nullptr) {
        errs.push_back(SemanticError(
            "SemanticError",
            "Function '" + node->name + "' is already declared in this scope (line " +
            std::to_string(existing->declaredAtLine) + ")",
            node->line, node->column));
        return;
    }
    symbolTable.declare(node->name, "function", node->line);
    functions[node->name] = node;

    // Analyse the function body in its own scope
    symbolTable.pushScope();
    // Parameter type is 'unknown' at analysis time; it will be resolved
    // at the call-site once return-type inference is added in Phase 3.
    for (const auto& param : node->params) symbolTable.declare(param, "unknown", node->line);
    currentReturnTypes.clear();
    ++functionDepth;
    for (auto& stmt : node->body) visit(stmt);
    --functionDepth;
    symbolTable.popScope();
    std::string inferred = "null";
    for (const auto& type : currentReturnTypes) {
        if (inferred == "null") inferred = type;
        else if (type == "unknown" || inferred == "unknown") inferred = "unknown";
        else if (type != inferred && !(numericTypes.count(type) && numericTypes.count(inferred))) {
            errs.push_back(SemanticError("TypeError", "Function '" + node->name + "' returns incompatible types '" + inferred + "' and '" + type + "'",
                                         node->line, node->column));
            inferred = "unknown";
        } else if (type == "decimal" || inferred == "decimal") inferred = "decimal";
    }
    functionReturnTypes[node->name] = inferred;
}

std::string SemanticAnalyzer::visitReturnStmt(std::shared_ptr<ReturnStatement> node) {
    if (functionDepth == 0) {
        errs.push_back(SemanticError("SemanticError", "Return statement is only valid inside a function",
                                     node->line, node->column));
    }
    const std::string type = visit(node->expression);
    if (functionDepth > 0) currentReturnTypes.push_back(type.empty() ? "unknown" : type);
    return type;
}

std::string SemanticAnalyzer::visitFunctionCall(std::shared_ptr<FunctionCall> node) {
    SymbolEntry* entry = symbolTable.lookup(node->name);
    if (entry == nullptr) {
        errs.push_back(SemanticError(
            "SemanticError",
            "Undefined function '" + node->name + "'",
            node->line, node->column));
        return "";
    }
    if (entry->type != "function") {
        errs.push_back(SemanticError(
            "TypeError",
            "'" + node->name + "' is not a function (it is a '" + entry->type + "')",
            node->line, node->column));
        return "";
    }
    // Validate argument arity and infer the call result from the declaration.
    auto it = functions.find(node->name);
    if (it != functions.end()) {
        const size_t expected = it->second->params.size();
        const size_t actual = node->arguments.size();
        if (expected != actual) {
            errs.push_back(SemanticError(
                "SemanticError",
                "Function '" + node->name + "' expects " + std::to_string(expected) + " argument(s), got " + std::to_string(actual),
                node->line, node->column));
        }
    }
    for (const auto& argument : node->arguments) visit(argument);
    auto returnType = functionReturnTypes.find(node->name);
    return returnType == functionReturnTypes.end() ? "unknown" : returnType->second;
}
