#include "rejection_explainer.h"
#include <algorithm>
#include <cctype>
#include <sstream>

std::string RejectionExplainer::toLower(const std::string& s) {
    std::string result = s;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return result;
}

std::vector<std::string> RejectionExplainer::splitWords(const std::string& s) {
    std::vector<std::string> words;
    std::istringstream stream(s);
    std::string word;
    while (stream >> word) {
        words.push_back(word);
    }
    return words;
}

std::string RejectionExplainer::removeStringLiterals(const std::string& s) {
    std::string result;
    bool inString = false;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '"') {
            if (!inString) {
                inString = true;
                result += "\"\"";
            } else {
                inString = false;
            }
        } else if (!inString) {
            result += s[i];
        }
    }
    return result;
}

int RejectionExplainer::editDistance(const std::string& a, const std::string& b) {
    int m = (int)a.size();
    int n = (int)b.size();
    std::vector<std::vector<int>> dp(m + 1, std::vector<int>(n + 1));

    for (int i = 0; i <= m; i++) dp[i][0] = i;
    for (int j = 0; j <= n; j++) dp[0][j] = j;

    for (int i = 1; i <= m; i++) {
        for (int j = 1; j <= n; j++) {
            int cost = (a[i-1] != b[j-1]) ? 1 : 0;
            dp[i][j] = std::min({dp[i-1][j] + 1, dp[i][j-1] + 1, dp[i-1][j-1] + cost});
        }
    }

    return dp[m][n];
}

RejectionExplainer::RejectionExplainer() {
    keywordCorrections["sey"] = "say";
    keywordCorrections["sya"] = "say";
    keywordCorrections["saay"] = "say";
    keywordCorrections["sett"] = "set";
    keywordCorrections["st"] = "set";
    keywordCorrections["ste"] = "set";
    keywordCorrections["ist"] = "if";
    keywordCorrections["iff"] = "if";
    keywordCorrections["thn"] = "then";
    keywordCorrections["thne"] = "then";
    keywordCorrections["thee"] = "then";
    keywordCorrections["othrwise"] = "otherwise";
    keywordCorrections["otherwsie"] = "otherwise";
    keywordCorrections["otherwize"] = "otherwise";
    keywordCorrections["els"] = "otherwise";
    keywordCorrections["else"] = "otherwise";
    keywordCorrections["endd"] = "end";
    keywordCorrections["ned"] = "end";
    keywordCorrections["print"] = "say";
    keywordCorrections["output"] = "say";
    keywordCorrections["echo"] = "say";
    keywordCorrections["display"] = "say";
    keywordCorrections["let"] = "set";
    keywordCorrections["var"] = "set";
    keywordCorrections["define"] = "set";
    keywordCorrections["assign"] = "set";
    keywordCorrections["tru"] = "yes";
    keywordCorrections["true"] = "yes";
    keywordCorrections["fals"] = "no";
    keywordCorrections["false"] = "no";
    // Phase-2 keyword corrections
    keywordCorrections["loop"] = "repeat while";
    keywordCorrections["while"] = "repeat while";
    keywordCorrections["for"] = "repeat";
    keywordCorrections["def"] = "create";
    keywordCorrections["function"] = "create";
    keywordCorrections["func"] = "create";
    keywordCorrections["fn"] = "create";
    keywordCorrections["return"] = "give back";
    keywordCorrections["ret"] = "give back";
    keywordCorrections["input"] = "ask";
    keywordCorrections["read"] = "ask";
    keywordCorrections["scan"] = "ask";
    keywordCorrections["invoke"] = "call";
    keywordCorrections["run"] = "call";
    keywordCorrections["execute"] = "call";

    validComparisons.push_back("is equal to");
    validComparisons.push_back("is not equal to");
    validComparisons.push_back("is greater than");
    validComparisons.push_back("is less than");
    validComparisons.push_back("is at least");
    validComparisons.push_back("is at most");
}

ErrorObject* RejectionExplainer::detectBadSetSyntax(
    const std::vector<std::string>& words, int line, const std::string& sourceLine) const {

    if (words.empty() || toLower(words[0]) != "set") return nullptr;

    std::vector<std::string> lowerWords;
    for (const auto& w : words) lowerWords.push_back(toLower(w));

    if (lowerWords.size() >= 4 && lowerWords[2] == "equal" && lowerWords[3] == "to") {
        std::string varName = words[1];
        std::string remaining;
        for (size_t i = 4; i < words.size(); i++) {
            if (i > 4) remaining += " ";
            remaining += words[i];
        }
        if (remaining.empty()) remaining = "...";
            std::string foundStr;
            for (size_t i = 0; i < words.size(); i++) {
                if (i > 0) foundStr += " ";
                foundStr += words[i];
            }
            return new ErrorObject(
                ErrorCategory::SYNTAX,
                "Unexpected word 'equal' in variable declaration",
                line, 1, sourceLine,
                "Use 'set " + varName + " to " + remaining + "' -- the keyword 'to' is used for assignment, not 'equal to'",
                foundStr,
                "set " + varName + " to " + remaining
            );
    }

    if (lowerWords.size() >= 3 && lowerWords[2] == "equals") {
        std::string varName = words[1];
        std::string remaining;
        for (size_t i = 3; i < words.size(); i++) {
            if (i > 3) remaining += " ";
            remaining += words[i];
        }
        if (remaining.empty()) remaining = "...";
        return new ErrorObject(
            ErrorCategory::SYNTAX,
            "Unexpected word 'equals' in variable declaration",
            line, 1, sourceLine,
            "Use 'set " + varName + " to " + remaining + "' -- assignments use 'to', not 'equals'",
            "", "set " + varName + " to " + remaining
        );
    }

    if (sourceLine.find('=') != std::string::npos && toLower(sourceLine).find("equal") == std::string::npos) {
        std::string varName = words.size() > 1 ? words[1] : "X";
        return new ErrorObject(
            ErrorCategory::SYNTAX,
            "The '=' symbol is not valid in Lucid",
            line, (int)sourceLine.find('=') + 1, sourceLine,
            "Use 'set " + varName + " to <value>' -- Lucid uses the word 'to' instead of '='",
            sourceLine, "set " + varName + " to ..."
        );
    }

    return nullptr;
}

ErrorObject* RejectionExplainer::detectBadComparison(
    const std::vector<std::string>& words, int line, const std::string& sourceLine) const {

    std::vector<std::string> lowerWords;
    for (const auto& w : words) lowerWords.push_back(toLower(w));

    for (int i = 0; i < (int)lowerWords.size(); i++) {
        if (lowerWords[i] != "is") continue;

        bool matched = false;
        std::vector<std::string> remaining(lowerWords.begin() + i, lowerWords.end());

        for (const auto& comp : validComparisons) {
            std::vector<std::string> compWords = splitWords(comp);
            if ((int)remaining.size() >= (int)compWords.size()) {
                bool match = true;
                for (int j = 0; j < (int)compWords.size(); j++) {
                    if (remaining[j] != compWords[j]) {
                        match = false;
                        break;
                    }
                }
                if (match) {
                    matched = true;
                    break;
                }
            }
        }

        if (!matched && i + 1 < (int)lowerWords.size()) {
            std::string badPhrase;
            for (int j = i; j < std::min(i + 4, (int)words.size()); j++) {
                if (lowerWords[j] == "then" || lowerWords[j] == "and" || lowerWords[j] == "or") break;
                if (j > i) badPhrase += " ";
                badPhrase += words[j];
            }

            std::string validList;
            for (size_t j = 0; j < validComparisons.size(); j++) {
                if (j > 0) validList += ", ";
                validList += "'" + validComparisons[j] + "'";
            }

            std::string lowerSource = toLower(sourceLine);
            int col = 1;
            size_t isPos = lowerSource.find("is");
            if (isPos != std::string::npos) col = (int)isPos + 1;

            return new ErrorObject(
                ErrorCategory::SYNTAX,
                "Unrecognized comparison phrase '" + badPhrase + "'",
                line, col, sourceLine,
                "'" + badPhrase + "' is not a recognized comparison. Valid comparisons are: " + validList,
                badPhrase, "a valid comparison operator"
            );
        }
    }

    return nullptr;
}

ErrorObject* RejectionExplainer::detectKeywordTypo(
    const std::string& word, int line, int column, const std::string& sourceLine) const {

    std::string lower = toLower(word);
    auto it = keywordCorrections.find(lower);
    if (it != keywordCorrections.end()) {
        return new ErrorObject(
            ErrorCategory::LEXICAL,
            "Unrecognized word '" + word + "'",
            line, column, sourceLine,
            "Did you mean '" + it->second + "'? ('" + word + "' is not a Lucid keyword)",
            word, it->second
        );
    }
    return nullptr;
}

std::string RejectionExplainer::findClosestKeyword(const std::string& word) const {
    std::vector<std::string> keywords = {
        "set", "to", "say", "if", "then", "otherwise", "end",
        "yes", "no", "plus", "minus", "times", "and", "or", "not"
    };

    std::string lower = toLower(word);
    std::string bestMatch;
    int bestDist = 3;

    for (const auto& kw : keywords) {
        int dist = editDistance(lower, kw);
        if (dist < bestDist) {
            bestDist = dist;
            bestMatch = kw;
        }
    }

    return bestMatch;
}

std::vector<ErrorObject> RejectionExplainer::explainLine(const std::string& sourceLine, int lineNumber) const {
    std::vector<ErrorObject> result;
    std::string stripped = sourceLine;

    while (!stripped.empty() && (stripped.front() == ' ' || stripped.front() == '\t'))
        stripped.erase(stripped.begin());
    while (!stripped.empty() && (stripped.back() == ' ' || stripped.back() == '\t' || stripped.back() == '\r' || stripped.back() == '\n'))
        stripped.pop_back();

    if (stripped.empty()) return result;

    std::string cleaned = removeStringLiterals(stripped);
    std::vector<std::string> words = splitWords(cleaned);

    ErrorObject* err = detectBadSetSyntax(words, lineNumber, stripped);
    if (err) {
        result.push_back(*err);
        delete err;
        return result;
    }

    err = detectBadComparison(words, lineNumber, stripped);
    if (err) {
        result.push_back(*err);
        delete err;
        return result;
    }

    if (!words.empty()) {
        err = detectKeywordTypo(words[0], lineNumber, 1, stripped);
        if (err) {
            result.push_back(*err);
            delete err;
            return result;
        }
    }

    return result;
}

ErrorObject RejectionExplainer::enhanceLexerError(
    const std::string& message, int line, int column, const std::string& sourceLine) const {

    std::string suggestion;
    std::string foundText;

    std::vector<std::string> words = splitWords(sourceLine);
    for (const auto& word : words) {
        ErrorObject* typoErr = detectKeywordTypo(word, line, column, sourceLine);
        if (typoErr) {
            suggestion = typoErr->suggestion;
            foundText = word;
            delete typoErr;
            break;
        }
    }

    if (suggestion.empty()) {
        for (const auto& word : words) {
            std::string closest = findClosestKeyword(word);
            if (!closest.empty()) {
                suggestion = "Did you mean '" + closest + "'?";
                foundText = word;
                break;
            }
        }
    }

    std::string stripped = sourceLine;
    while (!stripped.empty() && (stripped.back() == '\r' || stripped.back() == '\n'))
        stripped.pop_back();

    return ErrorObject(ErrorCategory::LEXICAL, message, line, column, stripped, suggestion, foundText);
}

ErrorObject RejectionExplainer::enhanceParserError(
    const std::string& message, int line, int column, const std::string& sourceLine) const {

    std::string stripped = sourceLine;
    while (!stripped.empty() && (stripped.back() == '\r' || stripped.back() == '\n'))
        stripped.pop_back();

    std::vector<std::string> words = splitWords(stripped);
    std::string suggestion;

    if (!words.empty()) {
        ErrorObject* err = detectBadSetSyntax(words, line, stripped);
        if (err) {
            ErrorObject result = *err;
            delete err;
            return result;
        }

        err = detectBadComparison(words, line, stripped);
        if (err) {
            ErrorObject result = *err;
            delete err;
            return result;
        }

        err = detectKeywordTypo(words[0], line, 1, stripped);
        if (err) {
            suggestion = err->suggestion;
            delete err;
        }
    }

    return ErrorObject(ErrorCategory::SYNTAX, message, line, column, stripped, suggestion);
}
