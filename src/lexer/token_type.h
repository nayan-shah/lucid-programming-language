#ifndef TOKEN_TYPE_H
#define TOKEN_TYPE_H

enum class TokenType {
    NUMBER,
    DECIMAL,
    STRING,
    IDENTIFIER,

    SET,
    TO,
    SAY,
    IF,
    THEN,
    OTHERWISE,
    END,
    YES,
    NO,
    NOTE,

    PLUS,
    MINUS,
    TIMES,
    DIVIDED_BY,

    IS_EQUAL_TO,
    IS_NOT_EQUAL_TO,
    IS_GREATER_THAN,
    IS_LESS_THAN,
    IS_AT_LEAST,
    IS_AT_MOST,

    AND,
    OR,
    NOT,

    REPEAT,
    WHILE,
    CREATE,
    ASK,
    GIVE_BACK,
    CALL,
    USING,

    LPAREN,
    RPAREN,
    COMMA,
    NEWLINE,
    EOF_TOKEN
};

#include <string>
#include <ostream>

inline std::string tokenTypeName(TokenType t) {
    switch (t) {
        case TokenType::NUMBER: return "NUMBER";
        case TokenType::DECIMAL: return "DECIMAL";
        case TokenType::STRING: return "STRING";
        case TokenType::IDENTIFIER: return "IDENTIFIER";
        case TokenType::SET: return "SET";
        case TokenType::TO: return "TO";
        case TokenType::SAY: return "SAY";
        case TokenType::IF: return "IF";
        case TokenType::THEN: return "THEN";
        case TokenType::OTHERWISE: return "OTHERWISE";
        case TokenType::END: return "END";
        case TokenType::YES: return "YES";
        case TokenType::NO: return "NO";
        case TokenType::NOTE: return "NOTE";
        case TokenType::PLUS: return "PLUS";
        case TokenType::MINUS: return "MINUS";
        case TokenType::TIMES: return "TIMES";
        case TokenType::DIVIDED_BY: return "DIVIDED_BY";
        case TokenType::IS_EQUAL_TO: return "IS_EQUAL_TO";
        case TokenType::IS_NOT_EQUAL_TO: return "IS_NOT_EQUAL_TO";
        case TokenType::IS_GREATER_THAN: return "IS_GREATER_THAN";
        case TokenType::IS_LESS_THAN: return "IS_LESS_THAN";
        case TokenType::IS_AT_LEAST: return "IS_AT_LEAST";
        case TokenType::IS_AT_MOST: return "IS_AT_MOST";
        case TokenType::AND: return "AND";
        case TokenType::OR: return "OR";
        case TokenType::NOT: return "NOT";
        case TokenType::REPEAT: return "REPEAT";
        case TokenType::WHILE: return "WHILE";
        case TokenType::CREATE: return "CREATE";
        case TokenType::ASK: return "ASK";
        case TokenType::GIVE_BACK: return "GIVE_BACK";
        case TokenType::CALL: return "CALL";
        case TokenType::USING: return "USING";
        case TokenType::LPAREN: return "LPAREN";
        case TokenType::RPAREN: return "RPAREN";
        case TokenType::COMMA: return "COMMA";
        case TokenType::NEWLINE: return "NEWLINE";
        case TokenType::EOF_TOKEN: return "EOF";
        default: return "UNKNOWN";
    }
}

inline std::ostream& operator<<(std::ostream& os, TokenType t) {
    os << tokenTypeName(t);
    return os;
}

#endif
