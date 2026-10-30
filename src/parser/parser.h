#ifndef PARSER_H
#define PARSER_H

#include <string>
#include <vector>
#include <memory>
#include <utility>
#include "../lexer/token.h"
#include "../ast/nodes.h"

struct ParseError {
    std::string message;
    int line;
    int column;

    ParseError(const std::string& msg, int ln, int col)
        : message(msg), line(ln), column(col) {}
};

class Parser {
public:
    Parser(const std::vector<Token>& tokens);
    std::pair<std::shared_ptr<Program>, std::vector<ParseError>> parse();

private:
    std::vector<Token> tokens;
    int pos;
    std::vector<ParseError> errors;

    std::shared_ptr<Program> parseProgram();
    ASTNodePtr parseStatement();
    ASTNodePtr parseVarDecl();
    ASTNodePtr parseOutputStmt();
    ASTNodePtr parseIfStmt();
    ASTNodePtr parseRepeatStmt();
    ASTNodePtr parseInputStmt();
    ASTNodePtr parseFunctionDecl();
    ASTNodePtr parseReturnStmt();
    ASTNodePtr parseFunctionCallNode();
    std::vector<ASTNodePtr> parseBlock();
    ASTNodePtr parseCondition();
    ASTNodePtr parseOrCondition();
    ASTNodePtr parseAndCondition();
    ASTNodePtr parseNotCondition();
    ASTNodePtr parseComparison();
    ASTNodePtr parseExpression();
    ASTNodePtr parseTerm();
    ASTNodePtr parseFactor();

    Token current() const;
    Token advance();
    bool check(TokenType type) const;
    bool atEnd() const;
    bool expect(TokenType type, const std::string& description);
    void expectNewline();
    void skipNewlines();
    void error(const std::string& message);
    void panicRecover();
    bool isCompOp(TokenType type) const;
};

#endif
