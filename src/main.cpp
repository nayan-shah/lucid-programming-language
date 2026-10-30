#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <algorithm>
#include <cstring>

#include "lexer/lexer.h"
#include "parser/parser.h"
#include "semantic/analyzer.h"
#include "errors/error_types.h"
#include "errors/rejection_explainer.h"
#include "runtime/interpreter.h"
#include "runtime/compiler_report.h"

std::string readFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << "Error: File '" << path << "' not found." << std::endl;
        exit(1);
    }
    std::ostringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

std::vector<std::string> splitLines(const std::string& s) {
    std::vector<std::string> lines;
    std::istringstream stream(s);
    std::string line;
    while (std::getline(stream, line)) {
        lines.push_back(line);
    }
    return lines;
}

std::string basename(const std::string& path) {
    size_t pos = path.find_last_of("/\\");
    if (pos == std::string::npos) return path;
    return path.substr(pos + 1);
}

void printHeader(const std::string& title) {
    std::cout << "\n" << std::string(60, '=') << "\n";
    std::cout << "  " << title << "\n";
    std::cout << std::string(60, '=') << "\n";
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: lucid <file.lc> [--show-tokens] [--show-ast] [--show-symbols]" << std::endl;
        return 1;
    }

    std::string filepath = argv[1];
    bool showTokens = false;
    bool showAst = false;
    bool showSymbols = false;
    bool emitJson = false;
    bool runProgram = false;
    bool interactiveInput = false;

    for (int i = 2; i < argc; i++) {
        if (std::strcmp(argv[i], "--show-tokens") == 0) showTokens = true;
        else if (std::strcmp(argv[i], "--show-ast") == 0) showAst = true;
        else if (std::strcmp(argv[i], "--show-symbols") == 0) showSymbols = true;
        else if (std::strcmp(argv[i], "--emit-json") == 0) emitJson = true;
        else if (std::strcmp(argv[i], "--run") == 0) runProgram = true;
        else if (std::strcmp(argv[i], "--interactive-input") == 0) interactiveInput = true;
    }

    std::string source = readFile(filepath);
    std::vector<std::string> sourceLines = splitLines(source);
    std::string filename = basename(filepath);

    if (!emitJson) {
        std::cout << "\nCompiling: " << filename << std::endl;
    }

    Lexer lexer(source);
    auto lexResult = lexer.tokenize();
    std::vector<Token> tokens = lexResult.first;
    std::vector<LexerError> lexErrors = lexResult.second;

    if (showTokens && !emitJson) {
        printHeader("Token Stream");
        for (const auto& tok : tokens) {
            std::cout << "  " << tok.shortStr() << "\n";
        }
    }

    Parser parser(tokens);
    auto parseResult = parser.parse();
    auto program = parseResult.first;
    std::vector<ParseError> parseErrors = parseResult.second;

    if (showAst && !emitJson) {
        printHeader("Abstract Syntax Tree");
        std::cout << program->prettyPrint() << "\n";
    }

    SemanticAnalyzer analyzer;
    std::vector<SemanticError> semanticErrors;
    if (lexErrors.empty() && parseErrors.empty()) {
        semanticErrors = analyzer.analyze(program);
    }

    Interpreter::ExecutionResult executionResult;
    executionResult.ok = true;
    bool compilationHasErrors = !lexErrors.empty() || !parseErrors.empty() || !semanticErrors.empty();
    if (runProgram && !compilationHasErrors) {
        Interpreter interpreter([interactiveInput](const std::string& name, std::string& value) {
            if (interactiveInput) {
                std::cerr << "LUCID_INPUT_REQUEST\t" << name << std::endl;
            } else {
                std::cerr << "Input required for " << name << ": " << std::flush;
            }
            return static_cast<bool>(std::getline(std::cin, value));
        });
        executionResult = interpreter.execute(program);
    }

    if (showSymbols && !emitJson) {
        printHeader("Symbol Table");
        std::cout << analyzer.symbolTable.formatTable() << "\n";
    }

    if (emitJson) {
        const Interpreter::ExecutionResult* reportExecution =
            (runProgram && !compilationHasErrors) ? &executionResult : nullptr;
        std::cout << buildCompilerReportJson(filename, tokens, lexErrors, parseErrors, semanticErrors, program, analyzer.symbolTable.getAllSymbols(), reportExecution, source);
        return compilationHasErrors || (reportExecution && !reportExecution->ok) ? 1 : 0;
    }

    RejectionExplainer explainer;

    std::vector<ErrorObject> allErrors;

    for (const auto& err : lexErrors) {
        std::string srcLine = (err.line > 0 && err.line <= (int)sourceLines.size()) ? sourceLines[err.line - 1] : "";
        allErrors.push_back(explainer.enhanceLexerError(err.message, err.line, err.column, srcLine));
    }

    for (const auto& err : parseErrors) {
        std::string srcLine = (err.line > 0 && err.line <= (int)sourceLines.size()) ? sourceLines[err.line - 1] : "";
        allErrors.push_back(explainer.enhanceParserError(err.message, err.line, err.column, srcLine));
    }

    for (const auto& err : semanticErrors) {
        std::string srcLine = (err.line > 0 && err.line <= (int)sourceLines.size()) ? sourceLines[err.line - 1] : "";
        ErrorCategory cat = (err.category == "TypeError") ? ErrorCategory::TYPE_ERROR : ErrorCategory::SEMANTIC;
        allErrors.push_back(ErrorObject(cat, err.message, err.line, err.column, srcLine));
    }

    for (int i = 0; i < (int)sourceLines.size(); i++) {
        auto lineErrors = explainer.explainLine(sourceLines[i], i + 1);
        for (const auto& re : lineErrors) {
            bool duplicate = false;
            for (const auto& existing : allErrors) {
                if (existing.line == re.line && existing.column == re.column) {
                    duplicate = true;
                    break;
                }
            }
            if (!duplicate) {
                allErrors.push_back(re);
            }
        }
    }

    std::sort(allErrors.begin(), allErrors.end(),
              [](const ErrorObject& a, const ErrorObject& b) {
                  if (a.line != b.line) return a.line < b.line;
                  return a.column < b.column;
              });

    if (!allErrors.empty()) {
        std::cout << formatErrorList(allErrors) << std::endl;
        return 1;
    }

    if (runProgram && !executionResult.ok) {
        std::cout << "\n[ERROR] Runtime execution failed: " << executionResult.error << "\n" << std::endl;
        return 1;
    }

    if (runProgram) {
        std::cout << "\n[OK] Compilation and execution successful.\n";
        if (!executionResult.output.empty()) {
            std::cout << "Runtime output:\n";
            for (const auto& line : executionResult.output) {
                std::cout << "  " << line << "\n";
            }
        }
        std::cout << std::endl;
        return 0;
    }

    std::cout << "\n[OK] Compilation successful -- no errors found.\n" << std::endl;
    return 0;
}
