#include "lexer.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <sstream>

Lexer::Lexer(const std::string& src) : source(src), pos(0), line(1), col(1) {
    splitLines();
}

void Lexer::splitLines() {
    std::istringstream stream(source);
    std::string ln;
    while (std::getline(stream, ln)) {
        sourceLines.push_back(ln);
    }
}

std::pair<std::vector<Token>, std::vector<LexerError>> Lexer::tokenize() {
    while (!atEnd()) {
        scanToken();
    }
    tokens.push_back(Token(TokenType::EOF_TOKEN, "", line, col));
    return {tokens, errors};
}

void Lexer::scanToken() {
    char c = peek();

    if (c == '\n') {
        emitNewline();
        return;
    }

    if (c == '\r') {
        advance();
        return;
    }

    if (c == ' ' || c == '\t') {
        advance();
        return;
    }

    if (c == '"') {
        scanString();
        return;
    }

    if (std::isdigit(static_cast<unsigned char>(c))) {
        scanNumber();
        return;
    }

    if (c == '(') {
        tokens.push_back(Token(TokenType::LPAREN, "(", line, col, "("));
        advance();
        return;
    }

    if (c == ')') {
        tokens.push_back(Token(TokenType::RPAREN, ")", line, col, ")"));
        advance();
        return;
    }

    if (c == ',') {
        tokens.push_back(Token(TokenType::COMMA, ",", line, col, ","));
        advance();
        return;
    }

    if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
        scanWordSequence();
        return;
    }

    std::string msg = "Unexpected character '";
    msg += c;
    msg += "'";
    errors.push_back(LexerError(msg, line, col, currentSourceLine()));
    advance();
}

void Lexer::scanString() {
    int startLine = line;
    int startCol = col;
    advance();

    std::string value;
    while (!atEnd() && peek() != '"') {
        if (peek() == '\n') {
            errors.push_back(LexerError("Unterminated string literal (hit newline)", startLine, startCol, currentSourceLine()));
            return;
        }
        if (peek() == '\\') {
            advance();
            if (!atEnd()) {
                char esc = peek();
                switch (esc) {
                    case 'n': value += '\n'; break;
                    case 't': value += '\t'; break;
                    case '\\': value += '\\'; break;
                    case '"': value += '"'; break;
                    default: value += esc; break;
                }
                advance();
            }
            continue;
        }
        value += peek();
        advance();
    }

    if (atEnd()) {
        errors.push_back(LexerError("Unterminated string literal (hit EOF)", startLine, startCol, currentSourceLine()));
        return;
    }

    advance();
    tokens.push_back(Token(TokenType::STRING, value, startLine, startCol, "\"" + value + "\""));
}

void Lexer::scanNumber() {
    int startCol = col;
    size_t startPos = pos;
    bool isDecimal = false;

    while (!atEnd() && std::isdigit(static_cast<unsigned char>(peek()))) {
        advance();
    }

    if (!atEnd() && peek() == '.') {
        size_t nextPos = pos + 1;
        if (nextPos < source.size() && std::isdigit(static_cast<unsigned char>(source[nextPos]))) {
            isDecimal = true;
            advance();
            while (!atEnd() && std::isdigit(static_cast<unsigned char>(peek()))) {
                advance();
            }
        }
    }

    std::string raw = source.substr(startPos, pos - startPos);
    try {
        const double value = std::stod(raw);
        if (!std::isfinite(value) || (!isDecimal && value > std::numeric_limits<int>::max())) {
            errors.push_back(LexerError("Numeric literal is outside the supported range", line, startCol, currentSourceLine()));
            return;
        }
        tokens.push_back(Token(isDecimal ? TokenType::DECIMAL : TokenType::NUMBER,
                               raw, line, startCol, value, raw));
    } catch (const std::exception&) {
        errors.push_back(LexerError("Numeric literal is outside the supported range", line, startCol, currentSourceLine()));
    }
}

void Lexer::scanWordSequence() {
    std::vector<std::tuple<std::string, int, int, size_t>> wordsWithPos;
    collectWords(wordsWithPos);

    int idx = 0;
    while (idx < (int)wordsWithPos.size()) {
        std::string word = std::get<0>(wordsWithPos[idx]);
        int wLine = std::get<1>(wordsWithPos[idx]);
        int wCol = std::get<2>(wordsWithPos[idx]);

        std::vector<std::string> remainingWords;
        for (int i = idx; i < (int)wordsWithPos.size(); i++) {
            remainingWords.push_back(std::get<0>(wordsWithPos[i]));
        }

        auto result = phraseTable.match(remainingWords, 0);
        if (result.first) {
            TokenType tokenType = result.second.first;
            int consumed = result.second.second;

            if (tokenType == TokenType::NOTE) {
                skipRestOfLine();
                return;
            }

            std::string raw;
            std::string val;
            for (int i = idx; i < idx + consumed; i++) {
                if (i > idx) {
                    raw += " ";
                    val += "_";
                }
                raw += std::get<0>(wordsWithPos[i]);
                std::string w = std::get<0>(wordsWithPos[i]);
                std::transform(w.begin(), w.end(), w.begin(),
                               [](unsigned char c) { return std::tolower(c); });
                val += w;
            }

            if (consumed == 1) {
                std::string w = word;
                std::transform(w.begin(), w.end(), w.begin(),
                               [](unsigned char c) { return std::tolower(c); });
                val = w;
            }

            tokens.push_back(Token(tokenType, val, wLine, wCol, raw));
            idx += consumed;
        } else {
            tokens.push_back(Token(TokenType::IDENTIFIER, word, wLine, wCol, word));
            idx += 1;
        }
    }
}

void Lexer::collectWords(std::vector<std::tuple<std::string, int, int, size_t>>& out) {
    while (!atEnd()) {
        char c = peek();

        if (c == ' ' || c == '\t') {
            advance();
            continue;
        }

        if (!(std::isalpha(static_cast<unsigned char>(c)) || c == '_')) {
            break;
        }

        int wordLine = line;
        int wordCol = col;
        size_t wordPos = pos;
        std::string word;

        while (!atEnd() && (std::isalpha(static_cast<unsigned char>(peek())) ||
                             std::isdigit(static_cast<unsigned char>(peek())) || peek() == '_')) {
            word += peek();
            advance();
        }

        std::string wordLower = word;
        std::transform(wordLower.begin(), wordLower.end(), wordLower.begin(),
                       [](unsigned char c) { return std::tolower(c); });

        if (wordLower == "note" && !atEnd() && peek() == ':') {
            advance();
            out.push_back(std::make_tuple(word, wordLine, wordCol, wordPos));
            return;
        }

        out.push_back(std::make_tuple(word, wordLine, wordCol, wordPos));

        size_t savedPos = pos;
        int savedCol = col;
        while (!atEnd() && (peek() == ' ' || peek() == '\t')) {
            advance();
        }

        if (atEnd() || !(std::isalpha(static_cast<unsigned char>(peek())) || peek() == '_')) {
            pos = savedPos;
            col = savedCol;
            break;
        }
    }
}

void Lexer::emitNewline() {
    if (!tokens.empty() && tokens.back().type != TokenType::NEWLINE) {
        tokens.push_back(Token(TokenType::NEWLINE, "\\n", line, col, "\\n"));
    }
    advance();
    line++;
    col = 1;
}

void Lexer::skipRestOfLine() {
    while (!atEnd() && peek() != '\n') {
        advance();
    }
}

char Lexer::peek() const {
    return source[pos];
}

void Lexer::advance() {
    if (source[pos] != '\n') {
        col++;
    }
    pos++;
}

bool Lexer::atEnd() const {
    return pos >= source.size();
}

std::string Lexer::currentSourceLine() const {
    int idx = line - 1;
    if (idx >= 0 && idx < (int)sourceLines.size()) {
        return sourceLines[idx];
    }
    return "";
}
