#ifndef ERROR_TYPES_H
#define ERROR_TYPES_H

#include <string>
#include <vector>
#include <sstream>

enum class ErrorCategory {
    LEXICAL,
    SYNTAX,
    SEMANTIC,
    TYPE_ERROR
};

inline std::string errorCategoryName(ErrorCategory cat) {
    switch (cat) {
        case ErrorCategory::LEXICAL: return "LexicalError";
        case ErrorCategory::SYNTAX: return "SyntaxError";
        case ErrorCategory::SEMANTIC: return "SemanticError";
        case ErrorCategory::TYPE_ERROR: return "TypeError";
        default: return "Error";
    }
}

struct ErrorObject {
    ErrorCategory category;
    std::string message;
    int line;
    int column;
    std::string sourceSnippet;
    std::string suggestion;
    std::string foundText;
    std::string expected;

    ErrorObject() : category(ErrorCategory::SYNTAX), line(0), column(0) {}

    ErrorObject(ErrorCategory cat, const std::string& msg, int ln, int col,
                const std::string& snippet = "", const std::string& sug = "",
                const std::string& found = "", const std::string& exp = "")
        : category(cat), message(msg), line(ln), column(col),
          sourceSnippet(snippet), suggestion(sug), foundText(found), expected(exp) {}

    std::string formatError(bool useColor = false) const;
};

std::string formatErrorList(const std::vector<ErrorObject>& errors, bool useColor = false);

#endif
