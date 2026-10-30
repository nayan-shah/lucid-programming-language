#include "interpreter.h"

#include <cerrno>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace {
struct ScopeGuard {
    explicit ScopeGuard(RuntimeEnvironment& environment) : env(environment) { env.pushScope(); }
    ~ScopeGuard() { env.popScope(); }
    RuntimeEnvironment& env;
};

struct ReturnSignal {
    explicit ReturnSignal(const RuntimeValue& result) : value(result) {}
    RuntimeValue value;
};

struct CallDepthGuard {
    explicit CallDepthGuard(int& depth) : value(depth) { ++value; }
    ~CallDepthGuard() { --value; }
    int& value;
};
}

RuntimeValue::RuntimeValue()
    : kind(Kind::Null), number(0.0), boolean(false), text("") {}

RuntimeValue RuntimeValue::numberValue(double value) {
    RuntimeValue v;
    v.kind = Kind::Number;
    v.number = value;
    return v;
}

RuntimeValue RuntimeValue::decimalValue(double value) {
    RuntimeValue v;
    v.kind = Kind::Decimal;
    v.number = value;
    return v;
}

RuntimeValue RuntimeValue::textValue(const std::string& value) {
    RuntimeValue v;
    v.kind = Kind::Text;
    v.text = value;
    return v;
}

RuntimeValue RuntimeValue::booleanValue(bool value) {
    RuntimeValue v;
    v.kind = Kind::Boolean;
    v.boolean = value;
    return v;
}

RuntimeValue RuntimeValue::nullValue() {
    return RuntimeValue();
}

std::string RuntimeValue::toString() const {
    switch (kind) {
        case Kind::Number:
            if (!std::isfinite(number) || number < std::numeric_limits<int>::min() ||
                number > std::numeric_limits<int>::max()) {
                throw std::runtime_error("Number value is outside the supported range");
            }
            return std::to_string(static_cast<int>(number));
        case Kind::Decimal:
            if (!std::isfinite(number)) throw std::runtime_error("Decimal value is not finite");
            return std::to_string(number);
        case Kind::Text:
            return text;
        case Kind::Boolean:
            return boolean ? "yes" : "no";
        case Kind::Null:
            return "null";
        default:
            return "";
    }
}

std::string RuntimeValue::typeName() const {
    switch (kind) {
        case Kind::Number: return "number";
        case Kind::Decimal: return "decimal";
        case Kind::Text: return "text";
        case Kind::Boolean: return "boolean";
        case Kind::Null: return "null";
        default: return "unknown";
    }
}

RuntimeEnvironment::RuntimeEnvironment() {
    pushScope();
}

void RuntimeEnvironment::pushScope() {
    scopes.push_back(std::map<std::string, RuntimeValue>());
}

void RuntimeEnvironment::popScope() {
    if (scopes.size() > 1) {
        scopes.pop_back();
    }
}

void RuntimeEnvironment::declare(const std::string& name, const RuntimeValue& value) {
    scopes.back()[name] = value;
}

void RuntimeEnvironment::declareOrAssign(const std::string& name, const RuntimeValue& value) {
    RuntimeValue* existing = lookupCurrentScope(name);
    if (existing != nullptr) {
        *existing = value;
        return;
    }

    for (int i = (int)scopes.size() - 1; i >= 0; --i) {
        auto it = scopes[i].find(name);
        if (it != scopes[i].end()) {
            it->second = value;
            return;
        }
    }

    declare(name, value);
}

RuntimeValue* RuntimeEnvironment::lookup(const std::string& name) {
    for (int i = (int)scopes.size() - 1; i >= 0; --i) {
        auto it = scopes[i].find(name);
        if (it != scopes[i].end()) {
            return &it->second;
        }
    }
    return nullptr;
}

RuntimeValue* RuntimeEnvironment::lookupCurrentScope(const std::string& name) {
    auto it = scopes.back().find(name);
    if (it != scopes.back().end()) {
        return &it->second;
    }
    return nullptr;
}

Interpreter::Interpreter(InputProvider provider)
    : callDepth(0), maxCallDepth(50), maxLoopIterations(100000),
      currentLine(0), currentColumn(0), inputProvider(std::move(provider)) {}

Interpreter::ExecutionResult Interpreter::execute(std::shared_ptr<Program> program) {
    ExecutionResult result{true, {}, {}, "", 0, 0};
    if (!program) {
        result.ok = false;
        result.error = "No program to execute.";
        return result;
    }

    try {
        env = RuntimeEnvironment();
        output.clear();
        events.clear();
        functions.clear();
        currentLine = 0;
        currentColumn = 0;
        for (const auto& stmt : program->statements) {
            executeStatement(stmt);
        }
        result.output = output;
        result.events = events;
        result.ok = true;
        return result;
    } catch (const std::runtime_error& e) {
        result.ok = false;
        result.error = e.what();
        result.line = currentLine;
        result.column = currentColumn;
        result.output = output;
        result.events = events;
        return result;
    }
}

RuntimeValue Interpreter::executeStatement(ASTNodePtr stmt) {
    if (stmt) {
        currentLine = stmt->line;
        currentColumn = stmt->column;
    }
    if (auto vd = std::dynamic_pointer_cast<VariableDeclaration>(stmt)) {
        RuntimeValue value = evaluateExpression(vd->expression);
        env.declareOrAssign(vd->name, value);
        events.push_back("declare " + vd->name + " = " + value.toString());
        return value;
    }

    if (auto ps = std::dynamic_pointer_cast<PrintStatement>(stmt)) {
        RuntimeValue value = evaluateExpression(ps->expression);
        output.push_back(value.toString());
        events.push_back("say " + value.toString());
        return value;
    }

    if (auto ifStmt = std::dynamic_pointer_cast<IfStatement>(stmt)) {
        RuntimeValue cond = evaluateExpression(ifStmt->condition);
        if (cond.kind != RuntimeValue::Kind::Boolean) {
            throw std::runtime_error("Condition in if statement must resolve to boolean at line " + std::to_string(ifStmt->line));
        }
        if (cond.boolean) {
            events.push_back("branch if line " + std::to_string(ifStmt->line) + " -> then");
            ScopeGuard scope(env);
            executeBlock(ifStmt->thenBlock);
        } else if (ifStmt->hasOtherwise) {
            events.push_back("branch if line " + std::to_string(ifStmt->line) + " -> otherwise");
            ScopeGuard scope(env);
            executeBlock(ifStmt->otherwiseBlock);
        } else {
            events.push_back("branch if line " + std::to_string(ifStmt->line) + " -> no branch");
        }
        return RuntimeValue::nullValue();
    }

    if (auto whileStmt = std::dynamic_pointer_cast<WhileStatement>(stmt)) {
        int iterations = 0;
        while (true) {
            RuntimeValue cond = evaluateExpression(whileStmt->condition);
            if (cond.kind != RuntimeValue::Kind::Boolean) {
                throw std::runtime_error("While condition must resolve to boolean at line " + std::to_string(whileStmt->line));
            }
            if (!cond.boolean) break;
            if (iterations >= maxLoopIterations) {
                throw std::runtime_error("Loop exceeded the maximum iteration count at line " + std::to_string(whileStmt->line));
            }
            events.push_back("loop while line " + std::to_string(whileStmt->line) +
                             " iteration " + std::to_string(iterations + 1));
            ScopeGuard scope(env);
            executeBlock(whileStmt->body);
            ++iterations;
        }
        return RuntimeValue::nullValue();
    }

    if (auto repeatStmt = std::dynamic_pointer_cast<RepeatTimesStatement>(stmt)) {
        RuntimeValue countValue = evaluateExpression(repeatStmt->count);
        if (countValue.kind != RuntimeValue::Kind::Number && countValue.kind != RuntimeValue::Kind::Decimal) {
            throw std::runtime_error("Repeat count must be numeric at line " + std::to_string(repeatStmt->line));
        }
        if (!std::isfinite(countValue.number) || countValue.number < 0 ||
            countValue.number > std::numeric_limits<int>::max() ||
            std::floor(countValue.number) != countValue.number) {
            throw std::runtime_error("Repeat count must be a non-negative whole number at line " + std::to_string(repeatStmt->line));
        }
        int count = static_cast<int>(countValue.number);
        if (count < 0) {
            throw std::runtime_error("Repeat count cannot be negative at line " + std::to_string(repeatStmt->line));
        }
        for (int i = 0; i < count && i < maxLoopIterations; ++i) {
            events.push_back("loop repeat line " + std::to_string(repeatStmt->line) +
                             " iteration " + std::to_string(i + 1));
            ScopeGuard scope(env);
            executeBlock(repeatStmt->body);
        }
        if (count > maxLoopIterations) {
            throw std::runtime_error("Repeat loop exceeded the maximum iteration count at line " + std::to_string(repeatStmt->line));
        }
        return RuntimeValue::nullValue();
    }

    if (auto inputStmt = std::dynamic_pointer_cast<InputStatement>(stmt)) {
        if (!inputProvider) {
            throw std::runtime_error("Interactive input is unavailable at line " + std::to_string(inputStmt->line));
        }
        std::string value;
        if (!inputProvider(inputStmt->varName, value)) {
            throw std::runtime_error("Input ended before a value was provided at line " + std::to_string(inputStmt->line));
        }
        RuntimeValue inputValue;
        if (inputStmt->inputType == "text") {
            inputValue = RuntimeValue::textValue(value);
        } else {
            errno = 0;
            char* end = nullptr;
            const double numericValue = std::strtod(value.c_str(), &end);
            while (end && *end != '\0' && std::isspace(static_cast<unsigned char>(*end))) ++end;
            const bool parsed = end != value.c_str() && end && *end == '\0' &&
                errno != ERANGE && std::isfinite(numericValue);
            const bool wholeNumber = inputStmt->inputType != "number" ||
                (std::floor(numericValue) == numericValue &&
                 numericValue >= std::numeric_limits<int>::min() &&
                 numericValue <= std::numeric_limits<int>::max());
            if (!parsed || !wholeNumber) {
                throw std::runtime_error("Input for '" + inputStmt->varName + "' must be a valid " +
                    inputStmt->inputType + " at line " + std::to_string(inputStmt->line));
            }
            inputValue = inputStmt->inputType == "number"
                ? RuntimeValue::numberValue(numericValue)
                : RuntimeValue::decimalValue(numericValue);
        }
        env.declareOrAssign(inputStmt->varName, inputValue);
        events.push_back("ask " + inputStmt->varName);
        return inputValue;
    }

    if (auto funcDecl = std::dynamic_pointer_cast<FunctionDeclaration>(stmt)) {
        functions[funcDecl->name] = funcDecl;
        events.push_back("define function " + funcDecl->name);
        return RuntimeValue::nullValue();
    }

    if (auto ret = std::dynamic_pointer_cast<ReturnStatement>(stmt)) {
        throw ReturnSignal(evaluateExpression(ret->expression));
    }

    if (auto call = std::dynamic_pointer_cast<FunctionCall>(stmt)) {
        return executeFunctionCall(call);
    }

    return RuntimeValue::nullValue();
}

RuntimeValue Interpreter::evaluateExpression(ASTNodePtr expr) {
    if (!expr) {
        return RuntimeValue::nullValue();
    }
    currentLine = expr->line;
    currentColumn = expr->column;

    if (auto lit = std::dynamic_pointer_cast<Literal>(expr)) {
        if (lit->valueType == "number") return RuntimeValue::numberValue(lit->numericValue);
        if (lit->valueType == "decimal") return RuntimeValue::decimalValue(lit->numericValue);
        if (lit->valueType == "text") return RuntimeValue::textValue(lit->value);
        if (lit->valueType == "boolean") return RuntimeValue::booleanValue(lit->boolValue);
        return RuntimeValue::nullValue();
    }

    if (auto vr = std::dynamic_pointer_cast<VariableReference>(expr)) {
        RuntimeValue* value = env.lookup(vr->name);
        if (value == nullptr) {
            throw std::runtime_error("Undefined variable '" + vr->name + "' at line " + std::to_string(vr->line));
        }
        return *value;
    }

    if (auto be = std::dynamic_pointer_cast<BinaryExpression>(expr)) {
        return evaluateBinary(be);
    }

    if (auto cmp = std::dynamic_pointer_cast<Comparison>(expr)) {
        return evaluateComparison(cmp);
    }

    if (auto le = std::dynamic_pointer_cast<LogicalExpression>(expr)) {
        return evaluateLogical(le);
    }

    if (auto ue = std::dynamic_pointer_cast<UnaryExpression>(expr)) {
        return evaluateUnary(ue);
    }

    if (auto call = std::dynamic_pointer_cast<FunctionCall>(expr)) {
        return executeFunctionCall(call);
    }

    return RuntimeValue::nullValue();
}

void Interpreter::executeBlock(const std::vector<ASTNodePtr>& block) {
    for (const auto& stmt : block) {
        executeStatement(stmt);
    }
}

RuntimeValue Interpreter::executeFunction(std::shared_ptr<FunctionDeclaration> functionDecl, const std::vector<RuntimeValue>& args) {
    if (args.size() != functionDecl->params.size()) {
        throw std::runtime_error("Function '" + functionDecl->name + "' received " +
            std::to_string(args.size()) + " argument(s), expected " + std::to_string(functionDecl->params.size()));
    }
    if (callDepth >= maxCallDepth) {
        throw std::runtime_error("Maximum function call depth exceeded at line " + std::to_string(functionDecl->line));
    }

    CallDepthGuard depth(callDepth);
    events.push_back("function enter " + functionDecl->name);
    ScopeGuard scope(env);
    for (size_t i = 0; i < functionDecl->params.size(); ++i) {
        env.declare(functionDecl->params[i], args[i]);
    }

    RuntimeValue value = RuntimeValue::nullValue();
    try {
        executeBlock(functionDecl->body);
    } catch (const ReturnSignal& result) {
        value = result.value;
    }

    events.push_back("function return " + functionDecl->name + " = " + value.toString());
    return value;
}

RuntimeValue Interpreter::executeFunctionCall(std::shared_ptr<FunctionCall> call) {
    auto it = functions.find(call->name);
    if (it == functions.end()) {
        throw std::runtime_error("Undefined function '" + call->name + "' at line " + std::to_string(call->line));
    }

    std::vector<RuntimeValue> argValues;
    for (const auto& argument : call->arguments) argValues.push_back(evaluateExpression(argument));

    events.push_back("function call " + call->name);
    return executeFunction(it->second, argValues);
}

RuntimeValue Interpreter::evaluateComparison(std::shared_ptr<Comparison> cmp) {
    RuntimeValue lhs = evaluateExpression(cmp->left);
    RuntimeValue rhs = evaluateExpression(cmp->right);

    auto compareNumbers = [&](double left, double right) {
        if (cmp->op == "is_equal_to") return left == right;
        if (cmp->op == "is_not_equal_to") return left != right;
        if (cmp->op == "is_greater_than") return left > right;
        if (cmp->op == "is_less_than") return left < right;
        if (cmp->op == "is_at_least") return left >= right;
        if (cmp->op == "is_at_most") return left <= right;
        throw std::runtime_error("Unsupported comparison operator: " + cmp->op);
    };

    if (lhs.kind == RuntimeValue::Kind::Text && rhs.kind == RuntimeValue::Kind::Text) {
        if (cmp->op == "is_equal_to") return RuntimeValue::booleanValue(lhs.text == rhs.text);
        if (cmp->op == "is_not_equal_to") return RuntimeValue::booleanValue(lhs.text != rhs.text);
        throw std::runtime_error("Text comparison operator not supported: " + cmp->op);
    }

    if (lhs.kind == RuntimeValue::Kind::Boolean && rhs.kind == RuntimeValue::Kind::Boolean) {
        bool left = lhs.boolean;
        bool right = rhs.boolean;
        if (cmp->op == "is_equal_to") return RuntimeValue::booleanValue(left == right);
        if (cmp->op == "is_not_equal_to") return RuntimeValue::booleanValue(left != right);
        throw std::runtime_error("Boolean comparison operator not supported: " + cmp->op);
    }

    if (lhs.kind == RuntimeValue::Kind::Null || rhs.kind == RuntimeValue::Kind::Null) {
        throw std::runtime_error("Cannot compare null value at line " + std::to_string(cmp->line));
    }

    return RuntimeValue::booleanValue(compareNumbers(lhs.number, rhs.number));
}

RuntimeValue Interpreter::evaluateLogical(std::shared_ptr<LogicalExpression> expr) {
    RuntimeValue left = evaluateExpression(expr->left);
    if (expr->op == "and") {
        if (left.kind != RuntimeValue::Kind::Boolean) {
            throw std::runtime_error("Logical 'and' requires boolean operands");
        }
        if (!left.boolean) return RuntimeValue::booleanValue(false);
        RuntimeValue right = evaluateExpression(expr->right);
        if (right.kind != RuntimeValue::Kind::Boolean) {
            throw std::runtime_error("Logical 'and' requires boolean operands");
        }
        return RuntimeValue::booleanValue(right.boolean);
    }

    if (expr->op == "or") {
        if (left.kind != RuntimeValue::Kind::Boolean) {
            throw std::runtime_error("Logical 'or' requires boolean operands");
        }
        if (left.boolean) return RuntimeValue::booleanValue(true);
        RuntimeValue right = evaluateExpression(expr->right);
        if (right.kind != RuntimeValue::Kind::Boolean) {
            throw std::runtime_error("Logical 'or' requires boolean operands");
        }
        return RuntimeValue::booleanValue(right.boolean);
    }

    throw std::runtime_error("Unsupported logical operator: " + expr->op);
}

RuntimeValue Interpreter::evaluateUnary(std::shared_ptr<UnaryExpression> expr) {
    RuntimeValue operand = evaluateExpression(expr->operand);
    if (expr->op == "not") {
        if (operand.kind != RuntimeValue::Kind::Boolean) {
            throw std::runtime_error("Unary 'not' requires a boolean operand");
        }
        return RuntimeValue::booleanValue(!operand.boolean);
    }
    throw std::runtime_error("Unsupported unary operator: " + expr->op);
}

RuntimeValue Interpreter::evaluateBinary(std::shared_ptr<BinaryExpression> expr) {
    RuntimeValue left = evaluateExpression(expr->left);
    RuntimeValue right = evaluateExpression(expr->right);

    if (left.kind == RuntimeValue::Kind::Text || right.kind == RuntimeValue::Kind::Text) {
        if (expr->op == "plus") {
            return RuntimeValue::textValue(left.toString() + right.toString());
        }
        throw std::runtime_error("String operation '" + expr->op + "' is not supported");
    }

    if (left.kind == RuntimeValue::Kind::Null || right.kind == RuntimeValue::Kind::Null) {
        throw std::runtime_error("Arithmetic operation requires numeric values");
    }

    double l = left.number;
    double r = right.number;

    const bool decimal = left.kind == RuntimeValue::Kind::Decimal || right.kind == RuntimeValue::Kind::Decimal;
    auto numericResult = [&](double value) {
        if (!std::isfinite(value)) throw std::runtime_error("Arithmetic result is not finite at line " + std::to_string(expr->line));
        if (!decimal && (value < std::numeric_limits<int>::min() || value > std::numeric_limits<int>::max())) {
            throw std::runtime_error("Number result is outside the supported range at line " + std::to_string(expr->line));
        }
        return decimal ? RuntimeValue::decimalValue(value) : RuntimeValue::numberValue(value);
    };

    if (expr->op == "plus") return numericResult(l + r);
    if (expr->op == "minus") return numericResult(l - r);
    if (expr->op == "times") return numericResult(l * r);
    if (expr->op == "divided_by") {
        if (r == 0.0) {
            throw std::runtime_error("Division by zero at line " + std::to_string(expr->line));
        }
        return numericResult(l / r);
    }

    throw std::runtime_error("Unsupported arithmetic operator: " + expr->op);
}

std::string Interpreter::runtimeTypeName(const RuntimeValue& value) const {
    return value.typeName();
}
