#ifndef PHRASE_TABLE_H
#define PHRASE_TABLE_H

#include <string>
#include <vector>
#include <map>
#include <utility>
#include "token_type.h"

struct PhraseEntry {
    std::vector<std::string> words;
    TokenType type;
};

class PhraseTable {
public:
    PhraseTable();
    std::pair<bool, std::pair<TokenType, int>> match(const std::vector<std::string>& words, int start) const;
    std::pair<bool, TokenType> isKeyword(const std::string& word) const;
    const std::vector<PhraseEntry>& allPhrases() const;
    const std::map<std::string, TokenType>& allKeywords() const;

private:
    std::vector<PhraseEntry> phrases;
    std::map<std::string, TokenType> keywords;
    static std::string toLower(const std::string& s);
};

#endif
