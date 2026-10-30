#ifndef SYMBOL_TABLE_H
#define SYMBOL_TABLE_H

#include <string>
#include <vector>
#include <map>
#include <sstream>
#include <iomanip>

struct SymbolEntry {
    std::string name;
    std::string type;
    int scopeId;
    int declaredAtLine;

    SymbolEntry() : scopeId(0), declaredAtLine(0) {}
    SymbolEntry(const std::string& n, const std::string& t, int sid, int ln)
        : name(n), type(t), scopeId(sid), declaredAtLine(ln) {}
};

class SymbolTable {
public:
    SymbolTable();
    int pushScope();
    void popScope();
    int currentScopeId() const;
    int depth() const;
    SymbolEntry* declare(const std::string& name, const std::string& varType, int line);
    SymbolEntry* lookup(const std::string& name);
    SymbolEntry* lookupCurrentScope(const std::string& name);
    std::vector<SymbolEntry> getAllSymbols() const;
    std::string formatTable() const;

private:
    std::vector<std::map<std::string, SymbolEntry>> scopes;
    std::vector<int> scopeIds;
    std::vector<SymbolEntry> declaredSymbols;
    int scopeCounter;
};

#endif
