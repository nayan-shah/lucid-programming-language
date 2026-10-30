#ifndef REJECTION_EXPLAINER_H
#define REJECTION_EXPLAINER_H

#include <string>
#include <vector>
#include <map>
#include "error_types.h"

class RejectionExplainer {
public:
    RejectionExplainer();
    std::vector<ErrorObject> explainLine(const std::string& sourceLine, int lineNumber) const;
    ErrorObject enhanceLexerError(const std::string& message, int line, int column, const std::string& sourceLine) const;
    ErrorObject enhanceParserError(const std::string& message, int line, int column, const std::string& sourceLine) const;

private:
    std::map<std::string, std::string> keywordCorrections;
    std::vector<std::string> validComparisons;

    ErrorObject* detectBadSetSyntax(const std::vector<std::string>& words, int line, const std::string& sourceLine) const;
    ErrorObject* detectBadComparison(const std::vector<std::string>& words, int line, const std::string& sourceLine) const;
    ErrorObject* detectKeywordTypo(const std::string& word, int line, int column, const std::string& sourceLine) const;
    std::string findClosestKeyword(const std::string& word) const;
    static int editDistance(const std::string& a, const std::string& b);
    static std::string toLower(const std::string& s);
    static std::vector<std::string> splitWords(const std::string& s);
    static std::string removeStringLiterals(const std::string& s);
};

#endif
