#ifndef INTERPRETER_H
#define INTERPRETER_H

#include <map>
#include <memory>
#include <functional>
#include <string>
#include <vector>

#include "../ast/nodes.h"

struct RuntimeValue {
    enum class Kind {
        Null,
        Number,
        Decimal,
        Text,
        Boolean
    };

    Kind kind;
    double number;
    bool boolean;
    std::string text;

    RuntimeValue();
    static RuntimeValue numberValue(double value);
    static RuntimeValue decimalValue(double value);
    static RuntimeValue textValue(const std::string& value);
    static RuntimeValue booleanValue(bool value);
    static RuntimeValue nullValue();

    std::string toString() const;
    std::string typeName() const;
};

class RuntimeEnvironment {
public:
    RuntimeEnvironment();
    void pushScope();
    void popScope();
    void declare(const std::string& name, const RuntimeValue& value);
    void declareOrAssign(const std::string& name, const RuntimeValue& value);
    RuntimeValue* lookup(const std::string& name);
    RuntimeValue* lookupCurrentScope(const std::string& name);

private:
    std::vector<std::map<std::string, RuntimeValue>> scopes;
};

class Interpreter {
public:
    using InputProvider = std::function<bool(const std::string&, std::string&)>;

    struct ExecutionResult {
        bool ok;
        std::vector<std::string> output;
        std::vector<std::string> events;
        std::string error;
        int line;
        int column;
    };

    explicit Interpreter(InputProvider inputProvider = InputProvider());
    ExecutionResult execute(std::shared_ptr<Program> program);

private:
    RuntimeEnvironment env;
    std::map<std::string, std::shared_ptr<FunctionDeclaration>> functions;
    std::vector<std::string> output;
    std::vector<std::string> events;
    int callDepth;
    int maxCallDepth;
    int maxLoopIterations;
    int currentLine;
    int currentColumn;
    InputProvider inputProvider;

    RuntimeValue executeStatement(ASTNodePtr stmt);
    RuntimeValue evaluateExpression(ASTNodePtr expr);
    void executeBlock(const std::vector<ASTNodePtr>& block);
    RuntimeValue executeFunction(std::shared_ptr<FunctionDeclaration> functionDecl, const std::vector<RuntimeValue>& args);
    RuntimeValue executeFunctionCall(std::shared_ptr<FunctionCall> call);
    RuntimeValue evaluateComparison(std::shared_ptr<Comparison> cmp);
    RuntimeValue evaluateLogical(std::shared_ptr<LogicalExpression> expr);
    RuntimeValue evaluateUnary(std::shared_ptr<UnaryExpression> expr);
    RuntimeValue evaluateBinary(std::shared_ptr<BinaryExpression> expr);
    std::string runtimeTypeName(const RuntimeValue& value) const;
};

#endif
