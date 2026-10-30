#ifndef NODES_H
#define NODES_H

#include <string>
#include <vector>
#include <memory>
#include <sstream>

class ASTNode {
public:
    int line;
    int column;

    ASTNode() : line(0), column(0) {}
    ASTNode(int ln, int col) : line(ln), column(col) {}
    virtual ~ASTNode() {}
    virtual std::string prettyPrint(int indent = 0) const = 0;
    virtual std::string nodeType() const = 0;

protected:
    std::string ind(int indent) const {
        return std::string(indent * 2, ' ');
    }
};

using ASTNodePtr = std::shared_ptr<ASTNode>;

class Literal : public ASTNode {
public:
    std::string value;
    std::string valueType;
    double numericValue;
    bool boolValue;

    Literal(const std::string& val, const std::string& type, int ln, int col)
        : ASTNode(ln, col), value(val), valueType(type), numericValue(0), boolValue(false) {}

    Literal(double num, const std::string& type, int ln, int col)
        : ASTNode(ln, col), valueType(type), numericValue(num), boolValue(false) {
        std::ostringstream ss;
        if (type == "number") {
            ss << (int)num;
        } else {
            ss << num;
        }
        value = ss.str();
    }

    Literal(bool b, int ln, int col)
        : ASTNode(ln, col), valueType("boolean"), numericValue(0), boolValue(b) {
        value = b ? "True" : "False";
    }

    std::string prettyPrint(int indent = 0) const override {
        return ind(indent) + "Literal: '" + value + "' (" + valueType + ")";
    }

    std::string nodeType() const override { return "Literal"; }
};

class VariableReference : public ASTNode {
public:
    std::string name;

    VariableReference(const std::string& n, int ln, int col)
        : ASTNode(ln, col), name(n) {}

    std::string prettyPrint(int indent = 0) const override {
        std::ostringstream ss;
        ss << ind(indent) << "VarRef: " << name << " (L" << line << ":C" << column << ")";
        return ss.str();
    }

    std::string nodeType() const override { return "VariableReference"; }
};

class BinaryExpression : public ASTNode {
public:
    ASTNodePtr left;
    std::string op;
    ASTNodePtr right;

    BinaryExpression(ASTNodePtr l, const std::string& o, ASTNodePtr r, int ln, int col)
        : ASTNode(ln, col), left(l), op(o), right(r) {}

    std::string prettyPrint(int indent = 0) const override {
        std::ostringstream ss;
        ss << ind(indent) << "BinaryExpr: " << op << " (L" << line << ":C" << column << ")\n";
        ss << left->prettyPrint(indent + 1) << "\n";
        ss << right->prettyPrint(indent + 1);
        return ss.str();
    }

    std::string nodeType() const override { return "BinaryExpression"; }
};

class Comparison : public ASTNode {
public:
    ASTNodePtr left;
    std::string op;
    ASTNodePtr right;

    Comparison(ASTNodePtr l, const std::string& o, ASTNodePtr r, int ln, int col)
        : ASTNode(ln, col), left(l), op(o), right(r) {}

    std::string prettyPrint(int indent = 0) const override {
        std::ostringstream ss;
        ss << ind(indent) << "Comparison: " << op << " (L" << line << ":C" << column << ")\n";
        ss << left->prettyPrint(indent + 1) << "\n";
        ss << right->prettyPrint(indent + 1);
        return ss.str();
    }

    std::string nodeType() const override { return "Comparison"; }
};

class LogicalExpression : public ASTNode {
public:
    ASTNodePtr left;
    std::string op;
    ASTNodePtr right;

    LogicalExpression(ASTNodePtr l, const std::string& o, ASTNodePtr r, int ln, int col)
        : ASTNode(ln, col), left(l), op(o), right(r) {}

    std::string prettyPrint(int indent = 0) const override {
        std::ostringstream ss;
        ss << ind(indent) << "LogicalExpr: " << op << " (L" << line << ":C" << column << ")\n";
        ss << left->prettyPrint(indent + 1) << "\n";
        ss << right->prettyPrint(indent + 1);
        return ss.str();
    }

    std::string nodeType() const override { return "LogicalExpression"; }
};

class UnaryExpression : public ASTNode {
public:
    std::string op;
    ASTNodePtr operand;

    UnaryExpression(const std::string& o, ASTNodePtr opnd, int ln, int col)
        : ASTNode(ln, col), op(o), operand(opnd) {}

    std::string prettyPrint(int indent = 0) const override {
        std::ostringstream ss;
        ss << ind(indent) << "UnaryExpr: " << op << " (L" << line << ":C" << column << ")\n";
        ss << operand->prettyPrint(indent + 1);
        return ss.str();
    }

    std::string nodeType() const override { return "UnaryExpression"; }
};

class VariableDeclaration : public ASTNode {
public:
    std::string name;
    ASTNodePtr expression;

    VariableDeclaration(const std::string& n, ASTNodePtr expr, int ln, int col)
        : ASTNode(ln, col), name(n), expression(expr) {}

    std::string prettyPrint(int indent = 0) const override {
        std::ostringstream ss;
        ss << ind(indent) << "VarDecl: " << name << " (L" << line << ":C" << column << ")";
        if (expression) {
            ss << "\n" << expression->prettyPrint(indent + 1);
        }
        return ss.str();
    }

    std::string nodeType() const override { return "VariableDeclaration"; }
};

class PrintStatement : public ASTNode {
public:
    ASTNodePtr expression;

    PrintStatement(ASTNodePtr expr, int ln, int col)
        : ASTNode(ln, col), expression(expr) {}

    std::string prettyPrint(int indent = 0) const override {
        std::ostringstream ss;
        ss << ind(indent) << "Say (L" << line << ":C" << column << ")";
        if (expression) {
            ss << "\n" << expression->prettyPrint(indent + 1);
        }
        return ss.str();
    }

    std::string nodeType() const override { return "PrintStatement"; }
};

class IfStatement : public ASTNode {
public:
    ASTNodePtr condition;
    std::vector<ASTNodePtr> thenBlock;
    std::vector<ASTNodePtr> otherwiseBlock;
    bool hasOtherwise;

    IfStatement(ASTNodePtr cond, const std::vector<ASTNodePtr>& tb,
                const std::vector<ASTNodePtr>& ob, bool hasElse, int ln, int col)
        : ASTNode(ln, col), condition(cond), thenBlock(tb),
          otherwiseBlock(ob), hasOtherwise(hasElse) {}

    std::string prettyPrint(int indent = 0) const override {
        std::ostringstream ss;
        ss << ind(indent) << "If (L" << line << ":C" << column << ")\n";
        ss << ind(indent + 1) << "Condition:\n";
        if (condition) {
            ss << condition->prettyPrint(indent + 2) << "\n";
        }
        ss << ind(indent + 1) << "Then:";
        for (const auto& stmt : thenBlock) {
            ss << "\n" << stmt->prettyPrint(indent + 2);
        }
        if (hasOtherwise) {
            ss << "\n" << ind(indent + 1) << "Otherwise:";
            for (const auto& stmt : otherwiseBlock) {
                ss << "\n" << stmt->prettyPrint(indent + 2);
            }
        }
        return ss.str();
    }

    std::string nodeType() const override { return "IfStatement"; }
};

class WhileStatement : public ASTNode {
public:
    ASTNodePtr condition;
    std::vector<ASTNodePtr> body;

    WhileStatement(ASTNodePtr cond, const std::vector<ASTNodePtr>& b, int ln, int col)
        : ASTNode(ln, col), condition(cond), body(b) {}

    std::string prettyPrint(int indent = 0) const override {
        std::ostringstream ss;
        ss << ind(indent) << "While (L" << line << ":C" << column << ")\n";
        ss << ind(indent + 1) << "Condition:\n";
        if (condition) ss << condition->prettyPrint(indent + 2) << "\n";
        ss << ind(indent + 1) << "Body:";
        for (const auto& stmt : body) ss << "\n" << stmt->prettyPrint(indent + 2);
        return ss.str();
    }

    std::string nodeType() const override { return "WhileStatement"; }
};

class RepeatTimesStatement : public ASTNode {
public:
    ASTNodePtr count;
    std::vector<ASTNodePtr> body;

    RepeatTimesStatement(ASTNodePtr cnt, const std::vector<ASTNodePtr>& b, int ln, int col)
        : ASTNode(ln, col), count(cnt), body(b) {}

    std::string prettyPrint(int indent = 0) const override {
        std::ostringstream ss;
        ss << ind(indent) << "RepeatTimes (L" << line << ":C" << column << ")\n";
        ss << ind(indent + 1) << "Count:\n";
        if (count) ss << count->prettyPrint(indent + 2) << "\n";
        ss << ind(indent + 1) << "Body:";
        for (const auto& stmt : body) ss << "\n" << stmt->prettyPrint(indent + 2);
        return ss.str();
    }

    std::string nodeType() const override { return "RepeatTimesStatement"; }
};

class InputStatement : public ASTNode {
public:
    std::string varName;
    std::string inputType;

    InputStatement(const std::string& name, int ln, int col, const std::string& type = "text")
        : ASTNode(ln, col), varName(name), inputType(type) {}

    std::string prettyPrint(int indent = 0) const override {
        std::ostringstream ss;
        ss << ind(indent) << "Ask: " << varName
           << (inputType == "text" ? "" : " as " + inputType)
           << " (L" << line << ":C" << column << ")";
        return ss.str();
    }

    std::string nodeType() const override { return "InputStatement"; }
};

class FunctionDeclaration : public ASTNode {
public:
    std::string name;
    std::string param;     // compatibility alias for the first parameter
    std::vector<std::string> params;
    std::vector<ASTNodePtr> body;

    FunctionDeclaration(const std::string& n, const std::string& p,
                        const std::vector<ASTNodePtr>& b, int ln, int col)
        : ASTNode(ln, col), name(n), param(p), params(p.empty() ? std::vector<std::string>() : std::vector<std::string>{p}), body(b) {}

    FunctionDeclaration(const std::string& n, const std::vector<std::string>& ps,
                        const std::vector<ASTNodePtr>& b, int ln, int col)
        : ASTNode(ln, col), name(n), param(ps.empty() ? "" : ps.front()), params(ps), body(b) {}

    std::string prettyPrint(int indent = 0) const override {
        std::ostringstream ss;
        ss << ind(indent) << "FuncDecl: " << name;
        if (!params.empty()) {
            ss << " using ";
            for (size_t i = 0; i < params.size(); ++i) { if (i) ss << ", "; ss << params[i]; }
        }
        ss << " (L" << line << ":C" << column << ")";
        for (const auto& stmt : body) ss << "\n" << stmt->prettyPrint(indent + 1);
        return ss.str();
    }

    std::string nodeType() const override { return "FunctionDeclaration"; }
};

class ReturnStatement : public ASTNode {
public:
    ASTNodePtr expression;

    ReturnStatement(ASTNodePtr expr, int ln, int col)
        : ASTNode(ln, col), expression(expr) {}

    std::string prettyPrint(int indent = 0) const override {
        std::ostringstream ss;
        ss << ind(indent) << "GiveBack (L" << line << ":C" << column << ")";
        if (expression) ss << "\n" << expression->prettyPrint(indent + 1);
        return ss.str();
    }

    std::string nodeType() const override { return "ReturnStatement"; }
};

class FunctionCall : public ASTNode {
public:
    std::string name;
    ASTNodePtr argument;   // compatibility alias for the first argument
    std::vector<ASTNodePtr> arguments;

    FunctionCall(const std::string& n, ASTNodePtr arg, int ln, int col)
        : ASTNode(ln, col), name(n), argument(arg), arguments(arg ? std::vector<ASTNodePtr>{arg} : std::vector<ASTNodePtr>()) {}

    FunctionCall(const std::string& n, const std::vector<ASTNodePtr>& args, int ln, int col)
        : ASTNode(ln, col), name(n), argument(args.empty() ? nullptr : args.front()), arguments(args) {}

    std::string prettyPrint(int indent = 0) const override {
        std::ostringstream ss;
        ss << ind(indent) << "Call: " << name
           << " (L" << line << ":C" << column << ")";
        for (const auto& arg : arguments) ss << "\n" << arg->prettyPrint(indent + 1);
        return ss.str();
    }

    std::string nodeType() const override { return "FunctionCall"; }
};

class Program : public ASTNode {
public:
    std::vector<ASTNodePtr> statements;

    Program() : ASTNode(0, 0) {}

    std::string prettyPrint(int indent = 0) const override {
        std::ostringstream ss;
        ss << ind(indent) << "Program";
        for (const auto& stmt : statements) {
            ss << "\n" << stmt->prettyPrint(indent + 1);
        }
        return ss.str();
    }

    std::string nodeType() const override { return "Program"; }
};

#endif
