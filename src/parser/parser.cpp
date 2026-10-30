#include "parser.h"
#include <algorithm>
#include <cctype>

Parser::Parser(const std::vector<Token>& toks) : tokens(toks), pos(0) {}

std::pair<std::shared_ptr<Program>, std::vector<ParseError>> Parser::parse() {
    auto program = parseProgram();
    return {program, errors};
}

bool Parser::isCompOp(TokenType type) const {
    return type == TokenType::IS_EQUAL_TO ||
           type == TokenType::IS_NOT_EQUAL_TO ||
           type == TokenType::IS_GREATER_THAN ||
           type == TokenType::IS_LESS_THAN ||
           type == TokenType::IS_AT_LEAST ||
           type == TokenType::IS_AT_MOST;
}

std::shared_ptr<Program> Parser::parseProgram() {
    auto program = std::make_shared<Program>();
    skipNewlines();

    while (!atEnd()) {
        auto stmt = parseStatement();
        if (stmt) {
            program->statements.push_back(stmt);
        }
        skipNewlines();
    }

    return program;
}

ASTNodePtr Parser::parseStatement() {
    Token tok = current();

    if (tok.type == TokenType::SET) {
        return parseVarDecl();
    } else if (tok.type == TokenType::SAY) {
        return parseOutputStmt();
    } else if (tok.type == TokenType::IF) {
        return parseIfStmt();
    } else if (tok.type == TokenType::REPEAT) {
        return parseRepeatStmt();
    } else if (tok.type == TokenType::ASK) {
        return parseInputStmt();
    } else if (tok.type == TokenType::CREATE) {
        return parseFunctionDecl();
    } else if (tok.type == TokenType::GIVE_BACK) {
        return parseReturnStmt();
    } else if (tok.type == TokenType::CALL) {
        auto node = parseFunctionCallNode();
        if (node) expectNewline();
        return node;
    } else {
        error("Expected statement (set, say, if, repeat, ask, create, give back, call), got '" + tok.value + "'");
        panicRecover();
        return nullptr;
    }
}

ASTNodePtr Parser::parseVarDecl() {
    Token setTok = current();
    if (!expect(TokenType::SET, "'set'")) {
        panicRecover();
        return nullptr;
    }

    Token nameTok = current();
    if (!expect(TokenType::IDENTIFIER, "variable name")) {
        panicRecover();
        return nullptr;
    }

    if (!expect(TokenType::TO, "'to'")) {
        panicRecover();
        return nullptr;
    }

    auto expr = parseExpression();
    if (!expr) {
        panicRecover();
        return nullptr;
    }

    expectNewline();

    return std::make_shared<VariableDeclaration>(nameTok.value, expr, setTok.line, setTok.column);
}

ASTNodePtr Parser::parseOutputStmt() {
    Token sayTok = current();
    if (!expect(TokenType::SAY, "'say'")) {
        panicRecover();
        return nullptr;
    }

    auto expr = parseExpression();
    if (!expr) {
        panicRecover();
        return nullptr;
    }

    expectNewline();

    return std::make_shared<PrintStatement>(expr, sayTok.line, sayTok.column);
}

ASTNodePtr Parser::parseIfStmt() {
    Token ifTok = current();
    if (!expect(TokenType::IF, "'if'")) {
        panicRecover();
        return nullptr;
    }

    auto condition = parseCondition();
    if (!condition) {
        panicRecover();
        return nullptr;
    }

    if (!expect(TokenType::THEN, "'then'")) {
        panicRecover();
        return nullptr;
    }

    expectNewline();
    skipNewlines();

    auto thenBlock = parseBlock();

    std::vector<ASTNodePtr> otherwiseBlock;
    bool hasOtherwise = false;
    if (check(TokenType::OTHERWISE)) {
        advance();
        expectNewline();
        skipNewlines();
        otherwiseBlock = parseBlock();
        hasOtherwise = true;
    }

    if (!expect(TokenType::END, "'end'")) {
        panicRecover();
        return std::make_shared<IfStatement>(condition, thenBlock, otherwiseBlock, hasOtherwise, ifTok.line, ifTok.column);
    }

    expectNewline();

    return std::make_shared<IfStatement>(condition, thenBlock, otherwiseBlock, hasOtherwise, ifTok.line, ifTok.column);
}

std::vector<ASTNodePtr> Parser::parseBlock() {
    std::vector<ASTNodePtr> statements;
    skipNewlines();

    while (!atEnd() && !check(TokenType::END) && !check(TokenType::OTHERWISE)) {
        auto stmt = parseStatement();
        if (stmt) {
            statements.push_back(stmt);
        }
        skipNewlines();
    }

    return statements;
}

ASTNodePtr Parser::parseCondition() {
    return parseOrCondition();
}

ASTNodePtr Parser::parseOrCondition() {
    auto left = parseAndCondition();
    if (!left) return nullptr;

    while (check(TokenType::OR)) {
        Token opTok = advance();
        auto right = parseAndCondition();
        if (!right) return left;
        left = std::make_shared<LogicalExpression>(left, "or", right, opTok.line, opTok.column);
    }

    return left;
}

ASTNodePtr Parser::parseAndCondition() {
    auto left = parseNotCondition();
    if (!left) return nullptr;

    while (check(TokenType::AND)) {
        Token opTok = advance();
        auto right = parseNotCondition();
        if (!right) return left;
        left = std::make_shared<LogicalExpression>(left, "and", right, opTok.line, opTok.column);
    }

    return left;
}

ASTNodePtr Parser::parseNotCondition() {
    if (check(TokenType::NOT)) {
        Token notTok = advance();
        auto operand = parseNotCondition();
        if (!operand) return nullptr;
        return std::make_shared<UnaryExpression>("not", operand, notTok.line, notTok.column);
    }
    return parseComparison();
}

ASTNodePtr Parser::parseComparison() {
    auto left = parseExpression();
    if (!left) return nullptr;

    if (isCompOp(current().type)) {
        Token opTok = advance();
        std::string opName = tokenTypeName(opTok.type);
        std::transform(opName.begin(), opName.end(), opName.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        auto right = parseExpression();
        if (!right) {
            error("Expected expression after comparison operator");
            return left;
        }
        return std::make_shared<Comparison>(left, opName, right, opTok.line, opTok.column);
    }

    return left;
}

ASTNodePtr Parser::parseExpression() {
    auto left = parseTerm();
    if (!left) return nullptr;

    while (check(TokenType::PLUS) || check(TokenType::MINUS)) {
        Token opTok = advance();
        std::string opName = tokenTypeName(opTok.type);
        std::transform(opName.begin(), opName.end(), opName.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        auto right = parseTerm();
        if (!right) {
            error("Expected term after arithmetic operator");
            return left;
        }
        left = std::make_shared<BinaryExpression>(left, opName, right, opTok.line, opTok.column);
    }

    return left;
}

ASTNodePtr Parser::parseTerm() {
    auto left = parseFactor();
    if (!left) return nullptr;

    while (check(TokenType::TIMES) || check(TokenType::DIVIDED_BY)) {
        Token opTok = advance();
        std::string opName = tokenTypeName(opTok.type);
        std::transform(opName.begin(), opName.end(), opName.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        auto right = parseFactor();
        if (!right) {
            error("Expected factor after arithmetic operator");
            return left;
        }
        left = std::make_shared<BinaryExpression>(left, opName, right, opTok.line, opTok.column);
    }

    return left;
}

ASTNodePtr Parser::parseFactor() {
    Token tok = current();

    if (tok.type == TokenType::NUMBER) {
        advance();
        return std::make_shared<Literal>(tok.numericValue, "number", tok.line, tok.column);
    }

    if (tok.type == TokenType::DECIMAL) {
        advance();
        return std::make_shared<Literal>(tok.numericValue, "decimal", tok.line, tok.column);
    }

    if (tok.type == TokenType::STRING) {
        advance();
        return std::make_shared<Literal>(tok.value, "text", tok.line, tok.column);
    }

    if (tok.type == TokenType::YES) {
        advance();
        return std::make_shared<Literal>(true, tok.line, tok.column);
    }

    if (tok.type == TokenType::NO) {
        advance();
        return std::make_shared<Literal>(false, tok.line, tok.column);
    }

    if (tok.type == TokenType::IDENTIFIER) {
        advance();
        return std::make_shared<VariableReference>(tok.value, tok.line, tok.column);
    }

    if (tok.type == TokenType::LPAREN) {
        advance();
        auto expr = parseExpression();
        expect(TokenType::RPAREN, "')'");
        return expr;
    }

    if (tok.type == TokenType::CALL) {
        return parseFunctionCallNode();
    }

    error("Expected expression, got '" + tok.value + "' (" + tokenTypeName(tok.type) + ")");
    return nullptr;
}

Token Parser::current() const {
    if (pos < (int)tokens.size()) {
        return tokens[pos];
    }
    return tokens.back();
}

Token Parser::advance() {
    Token tok = current();
    if (pos < (int)tokens.size() - 1) {
        pos++;
    }
    return tok;
}

bool Parser::check(TokenType type) const {
    return current().type == type;
}

bool Parser::atEnd() const {
    return current().type == TokenType::EOF_TOKEN;
}

bool Parser::expect(TokenType type, const std::string& description) {
    if (check(type)) {
        advance();
        return true;
    }
    Token tok = current();
    error("Expected " + description + ", got '" + tok.value + "' (" + tokenTypeName(tok.type) + ")");
    return false;
}

void Parser::expectNewline() {
    if (check(TokenType::NEWLINE)) {
        advance();
    } else if (check(TokenType::EOF_TOKEN)) {
    } else {
        Token tok = current();
        error("Expected newline after statement, got '" + tok.value + "'");
    }
}

void Parser::skipNewlines() {
    while (check(TokenType::NEWLINE)) {
        advance();
    }
}

void Parser::error(const std::string& message) {
    Token tok = current();
    errors.push_back(ParseError(message, tok.line, tok.column));
}

void Parser::panicRecover() {
    while (!atEnd() && !check(TokenType::NEWLINE)) {
        advance();
    }
    if (check(TokenType::NEWLINE)) {
        advance();
    }
}

// ── New statement parsers ─────────────────────────────────────────────────────

ASTNodePtr Parser::parseRepeatStmt() {
    Token repTok = current();
    if (!expect(TokenType::REPEAT, "'repeat'")) { panicRecover(); return nullptr; }

    if (check(TokenType::WHILE)) {
        // repeat while <condition> ... end
        advance(); // consume 'while'
        auto condition = parseCondition();
        if (!condition) { panicRecover(); return nullptr; }
        expectNewline();
        skipNewlines();
        auto body = parseBlock();
        if (!expect(TokenType::END, "'end'")) {
            return std::make_shared<WhileStatement>(condition, body, repTok.line, repTok.column);
        }
        expectNewline();
        return std::make_shared<WhileStatement>(condition, body, repTok.line, repTok.column);
    } else {
        // repeat <count> times ... end
        // Use parseFactor (not parseExpression) so TIMES is not consumed as
        // a multiplication operator before we can check for it ourselves.
        auto count = parseFactor();
        if (!count) { panicRecover(); return nullptr; }
        if (!expect(TokenType::TIMES, "'times'")) { panicRecover(); return nullptr; }
        expectNewline();
        skipNewlines();
        auto body = parseBlock();
        if (!expect(TokenType::END, "'end'")) {
            return std::make_shared<RepeatTimesStatement>(count, body, repTok.line, repTok.column);
        }
        expectNewline();
        return std::make_shared<RepeatTimesStatement>(count, body, repTok.line, repTok.column);
    }
}

ASTNodePtr Parser::parseInputStmt() {
    Token askTok = current();
    if (!expect(TokenType::ASK, "'ask'")) { panicRecover(); return nullptr; }
    Token nameTok = current();
    if (!expect(TokenType::IDENTIFIER, "variable name")) { panicRecover(); return nullptr; }
    std::string inputType = "text";
    if (check(TokenType::IDENTIFIER) && current().value == "as") {
        advance();
        Token typeTok = current();
        if (!expect(TokenType::IDENTIFIER, "input type 'number', 'decimal', or 'text'")) {
            panicRecover();
            return nullptr;
        }
        if (typeTok.value != "number" && typeTok.value != "decimal" && typeTok.value != "text") {
            error("Input type must be 'number', 'decimal', or 'text'");
            panicRecover();
            return nullptr;
        }
        inputType = typeTok.value;
    }
    expectNewline();
    return std::make_shared<InputStatement>(nameTok.value, askTok.line, askTok.column, inputType);
}

ASTNodePtr Parser::parseFunctionDecl() {
    Token createTok = current();
    if (!expect(TokenType::CREATE, "'create'")) { panicRecover(); return nullptr; }
    Token nameTok = current();
    if (!expect(TokenType::IDENTIFIER, "function name")) { panicRecover(); return nullptr; }

    std::vector<std::string> params;
    if (check(TokenType::USING)) {
        advance(); // consume 'using'
        while (true) {
            Token paramTok = current();
            if (!expect(TokenType::IDENTIFIER, "parameter name")) { panicRecover(); return nullptr; }
            params.push_back(paramTok.value);
            if (!check(TokenType::COMMA)) break;
            advance();
        }
    }

    expectNewline();
    skipNewlines();
    auto body = parseBlock();
    if (!expect(TokenType::END, "'end'")) {
        return std::make_shared<FunctionDeclaration>(nameTok.value, params, body, createTok.line, createTok.column);
    }
    expectNewline();
    return std::make_shared<FunctionDeclaration>(nameTok.value, params, body, createTok.line, createTok.column);
}

ASTNodePtr Parser::parseReturnStmt() {
    Token giveTok = current();
    if (!expect(TokenType::GIVE_BACK, "'give back'")) { panicRecover(); return nullptr; }
    auto expr = parseExpression();
    if (!expr) { panicRecover(); return nullptr; }
    expectNewline();
    return std::make_shared<ReturnStatement>(expr, giveTok.line, giveTok.column);
}

ASTNodePtr Parser::parseFunctionCallNode() {
    Token callTok = current();
    if (!expect(TokenType::CALL, "'call'")) { panicRecover(); return nullptr; }
    Token nameTok = current();
    if (!expect(TokenType::IDENTIFIER, "function name")) { panicRecover(); return nullptr; }

    std::vector<ASTNodePtr> args;
    if (check(TokenType::USING)) {
        advance(); // consume 'using'
        while (true) {
            auto arg = parseExpression();
            if (!arg) { panicRecover(); return nullptr; }
            args.push_back(arg);
            if (!check(TokenType::COMMA)) break;
            advance();
        }
    }

    return std::make_shared<FunctionCall>(nameTok.value, args, callTok.line, callTok.column);
}
