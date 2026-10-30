#include "phrase_table.h"
#include <algorithm>
#include <cctype>

std::string PhraseTable::toLower(const std::string& s) {
    std::string result = s;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return result;
}

PhraseTable::PhraseTable() {
    phrases.push_back({{"give", "back"}, TokenType::GIVE_BACK});
    phrases.push_back({{"is", "not", "equal", "to"}, TokenType::IS_NOT_EQUAL_TO});
    phrases.push_back({{"is", "equal", "to"}, TokenType::IS_EQUAL_TO});
    phrases.push_back({{"is", "greater", "than"}, TokenType::IS_GREATER_THAN});
    phrases.push_back({{"is", "less", "than"}, TokenType::IS_LESS_THAN});
    phrases.push_back({{"is", "at", "least"}, TokenType::IS_AT_LEAST});
    phrases.push_back({{"is", "at", "most"}, TokenType::IS_AT_MOST});
    phrases.push_back({{"divided", "by"}, TokenType::DIVIDED_BY});

    keywords["set"] = TokenType::SET;
    keywords["to"] = TokenType::TO;
    keywords["say"] = TokenType::SAY;
    keywords["if"] = TokenType::IF;
    keywords["then"] = TokenType::THEN;
    keywords["otherwise"] = TokenType::OTHERWISE;
    keywords["end"] = TokenType::END;
    keywords["yes"] = TokenType::YES;
    keywords["no"] = TokenType::NO;
    keywords["note"] = TokenType::NOTE;
    keywords["plus"] = TokenType::PLUS;
    keywords["minus"] = TokenType::MINUS;
    keywords["times"] = TokenType::TIMES;
    keywords["and"] = TokenType::AND;
    keywords["or"] = TokenType::OR;
    keywords["not"] = TokenType::NOT;
    keywords["repeat"] = TokenType::REPEAT;
    keywords["while"] = TokenType::WHILE;
    keywords["create"] = TokenType::CREATE;
    keywords["ask"] = TokenType::ASK;
    keywords["call"] = TokenType::CALL;
    keywords["using"] = TokenType::USING;
}

std::pair<bool, std::pair<TokenType, int>> PhraseTable::match(
    const std::vector<std::string>& words, int start) const {

    int remaining = (int)words.size() - start;

    for (const auto& entry : phrases) {
        int length = (int)entry.words.size();
        if (length <= remaining) {
            bool matched = true;
            for (int i = 0; i < length; i++) {
                if (toLower(words[start + i]) != entry.words[i]) {
                    matched = false;
                    break;
                }
            }
            if (matched) {
                return {true, {entry.type, length}};
            }
        }
    }

    std::string wordLower = toLower(words[start]);
    auto it = keywords.find(wordLower);
    if (it != keywords.end()) {
        return {true, {it->second, 1}};
    }

    return {false, {TokenType::EOF_TOKEN, 0}};
}

std::pair<bool, TokenType> PhraseTable::isKeyword(const std::string& word) const {
    std::string lower = toLower(word);
    auto it = keywords.find(lower);
    if (it != keywords.end()) {
        return {true, it->second};
    }
    return {false, TokenType::EOF_TOKEN};
}

const std::vector<PhraseEntry>& PhraseTable::allPhrases() const {
    return phrases;
}

const std::map<std::string, TokenType>& PhraseTable::allKeywords() const {
    return keywords;
}
