#ifndef ANALYZER_H
#define ANALYZER_H

#include <string>
#include <vector>
#include <memory>
#include <set>
#include <map>
#include "../ast/nodes.h"
#include "symbol_table.h"

struct SemanticError {
    std::string category;
    std::string message;
    int line;
    int column;

    SemanticError(const std::string& cat, const std::string& msg, int ln, int col)
        : category(cat), message(msg), line(ln), column(col) {}
};

class SemanticAnalyzer {
public:
    SymbolTable symbolTable;

    SemanticAnalyzer();
    std::vector<SemanticError> analyze(std::shared_ptr<Program> program);

private:
    std::vector<SemanticError> errs;
    int functionDepth;
    std::vector<std::string> currentReturnTypes;
    std::map<std::string, std::string> functionReturnTypes;
    static const std::set<std::string> numericTypes;

    std::string visit(ASTNodePtr node);
    void visitVarDecl(std::shared_ptr<VariableDeclaration> node);
    void visitPrintStmt(std::shared_ptr<PrintStatement> node);
    void visitIfStmt(std::shared_ptr<IfStatement> node);
    std::string visitBinaryExpr(std::shared_ptr<BinaryExpression> node);
    std::string visitComparison(std::shared_ptr<Comparison> node);
    std::string visitLogicalExpr(std::shared_ptr<LogicalExpression> node);
    std::string visitUnaryExpr(std::shared_ptr<UnaryExpression> node);
    std::string visitLiteral(std::shared_ptr<Literal> node);
    std::string visitVarRef(std::shared_ptr<VariableReference> node);

    // Phase-2 visitors
    void visitWhileStmt(std::shared_ptr<WhileStatement> node);
    void visitRepeatTimesStmt(std::shared_ptr<RepeatTimesStatement> node);
    void visitInputStmt(std::shared_ptr<InputStatement> node);
    void visitFunctionDecl(std::shared_ptr<FunctionDeclaration> node);
    std::string visitReturnStmt(std::shared_ptr<ReturnStatement> node);
    std::string visitFunctionCall(std::shared_ptr<FunctionCall> node);

    // Registered functions: name -> declaration node
    std::map<std::string, std::shared_ptr<FunctionDeclaration>> functions;
};

#endif
