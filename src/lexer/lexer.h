#ifndef LEXER_H
#define LEXER_H

#include <string>
#include <vector>
#include <utility>
#include "token.h"
#include "phrase_table.h"

struct LexerError {
    std::string message;
    int line;
    int column;
    std::string sourceLine;

    LexerError(const std::string& msg, int ln, int col, const std::string& src = "")
        : message(msg), line(ln), column(col), sourceLine(src) {}
};

class Lexer {
public:
    Lexer(const std::string& source);
    std::pair<std::vector<Token>, std::vector<LexerError>> tokenize();

private:
    std::string source;
    std::vector<Token> tokens;
    std::vector<LexerError> errors;
    PhraseTable phraseTable;
    size_t pos;
    int line;
    int col;
    std::vector<std::string> sourceLines;

    void scanToken();
    void scanString();
    void scanNumber();
    void scanWordSequence();
    void collectWords(std::vector<std::tuple<std::string, int, int, size_t>>& out);
    void emitNewline();
    void skipRestOfLine();
    char peek() const;
    void advance();
    bool atEnd() const;
    std::string currentSourceLine() const;
    void splitLines();
};

#endif
