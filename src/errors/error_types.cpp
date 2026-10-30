#include "error_types.h"

std::string ErrorObject::formatError(bool useColor) const {
    (void)useColor;
    std::ostringstream ss;

    std::string catName = errorCategoryName(category);
    ss << catName << " at line " << line << ", column " << column << "\n";
    ss << "  " << message << "\n";

    if (!sourceSnippet.empty()) {
        ss << "\n";
        std::ostringstream prefix;
        prefix << "  " << line << " | ";
        std::string pfx = prefix.str();
        ss << pfx << sourceSnippet << "\n";
        int caretOffset = (int)pfx.size() + std::max(0, column - 1);
        ss << std::string(caretOffset, ' ') << "^\n";
    }

    if (!suggestion.empty()) {
        ss << "\n  Suggestion: " << suggestion << "\n";
    }

    return ss.str();
}

std::string formatErrorList(const std::vector<ErrorObject>& errors, bool useColor) {
    if (errors.empty()) return "";

    std::ostringstream ss;
    ss << "\n" << std::string(60, '=') << "\n";
    ss << errors.size() << " error(s) found\n";
    ss << std::string(60, '=') << "\n";

    for (size_t i = 0; i < errors.size(); i++) {
        if (i > 0) ss << "\n";
        ss << errors[i].formatError(useColor);
    }

    return ss.str();
}
