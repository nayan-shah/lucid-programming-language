#ifndef TOKEN_H
#define TOKEN_H

#include <string>
#include <sstream>
#include <iomanip>
#include "token_type.h"

struct Token {
    TokenType type;
    std::string value;
    int line;
    int column;
    std::string raw;
    double numericValue;
    bool hasNumeric;

    Token() : type(TokenType::EOF_TOKEN), line(0), column(0), numericValue(0), hasNumeric(false) {}

    Token(TokenType t, const std::string& val, int ln, int col, const std::string& r = "")
        : type(t), value(val), line(ln), column(col), raw(r), numericValue(0), hasNumeric(false) {}

    Token(TokenType t, const std::string& val, int ln, int col, double num, const std::string& r = "")
        : type(t), value(val), line(ln), column(col), raw(r), numericValue(num), hasNumeric(true) {}

    std::string repr() const {
        std::ostringstream ss;
        ss << "Token(" << tokenTypeName(type) << ", '" << value << "', line=" << line << ", col=" << column << ")";
        return ss.str();
    }

    std::string shortStr() const {
        std::ostringstream ss;
        ss << std::left << std::setw(20) << tokenTypeName(type)
           << std::setw(20) << ("'" + value + "'")
           << "L" << line << ":C" << column;
        return ss.str();
    }
};

#endif
