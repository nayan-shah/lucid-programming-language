#include "symbol_table.h"

SymbolTable::SymbolTable() : scopeCounter(0) {
    pushScope();
}

int SymbolTable::pushScope() {
    scopes.push_back(std::map<std::string, SymbolEntry>());
    scopeCounter++;
    scopeIds.push_back(scopeCounter);
    return scopeCounter;
}

void SymbolTable::popScope() {
    if (scopes.size() > 1) {
        scopes.pop_back();
        scopeIds.pop_back();
    }
}

int SymbolTable::currentScopeId() const {
    return scopeIds.back();
}

int SymbolTable::depth() const {
    return (int)scopes.size();
}

SymbolEntry* SymbolTable::declare(const std::string& name, const std::string& varType, int line) {
    auto& current = scopes.back();
    if (current.find(name) != current.end()) {
        return nullptr;
    }

    current[name] = SymbolEntry(name, varType, currentScopeId(), line);
    declaredSymbols.push_back(current[name]);
    return &current[name];
}

SymbolEntry* SymbolTable::lookup(const std::string& name) {
    for (int i = (int)scopes.size() - 1; i >= 0; i--) {
        auto it = scopes[i].find(name);
        if (it != scopes[i].end()) {
            return &it->second;
        }
    }
    return nullptr;
}

SymbolEntry* SymbolTable::lookupCurrentScope(const std::string& name) {
    auto it = scopes.back().find(name);
    if (it != scopes.back().end()) {
        return &it->second;
    }
    return nullptr;
}

std::vector<SymbolEntry> SymbolTable::getAllSymbols() const {
    return declaredSymbols;
}

std::string SymbolTable::formatTable() const {
    std::ostringstream ss;
    ss << std::string(60, '=') << "\n";
    ss << std::left << std::setw(15) << "Name"
       << std::setw(10) << "Type"
       << std::setw(8) << "Scope"
       << std::setw(6) << "Line" << "\n";
    ss << std::string(60, '-') << "\n";

    for (int i = 0; i < (int)scopes.size(); i++) {
        for (const auto& pair : scopes[i]) {
            const SymbolEntry& entry = pair.second;
            std::string scopeLabel = (entry.scopeId == scopeIds.front())
                ? "global"
                : "block-" + std::to_string(entry.scopeId);
            ss << std::left << std::setw(15) << entry.name
               << std::setw(10) << entry.type
               << std::setw(8) << scopeLabel
               << std::setw(6) << entry.declaredAtLine << "\n";
        }
    }

    ss << std::string(60, '=');
    return ss.str();
}
