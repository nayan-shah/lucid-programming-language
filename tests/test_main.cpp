#include <iostream>
#include <string>
#include <vector>
#include <cassert>
#include <sstream>
#include <cstdlib>

#include "lexer/token_type.h"
#include "lexer/token.h"
#include "lexer/phrase_table.h"
#include "lexer/lexer.h"
#include "ast/nodes.h"
#include "parser/parser.h"
#include "semantic/symbol_table.h"
#include "semantic/analyzer.h"
#include "errors/error_types.h"
#include "errors/rejection_explainer.h"
#include "runtime/interpreter.h"
#include "runtime/compiler_report.h"

static int testsPassed = 0;
static int testsFailed = 0;

#define TEST(name) void name()
#define RUN(name) do { \
    try { name(); testsPassed++; std::cout << "  PASS: " #name << "\n"; } \
    catch (const std::exception& e) { testsFailed++; std::cout << "  FAIL: " #name << " - " << e.what() << "\n"; } \
    catch (...) { testsFailed++; std::cout << "  FAIL: " #name << " - unknown error\n"; } \
} while(0)

#define ASSERT_EQ(a, b) do { if ((a) != (b)) { \
    std::ostringstream ss; ss << "Expected " << (b) << " but got " << (a) << " at line " << __LINE__; \
    throw std::runtime_error(ss.str()); } } while(0)

#define ASSERT_TRUE(x) do { if (!(x)) { \
    std::ostringstream ss; ss << "Assertion failed at line " << __LINE__; \
    throw std::runtime_error(ss.str()); } } while(0)

std::pair<std::shared_ptr<Program>, std::vector<ParseError>> parseSource(const std::string& source) {
    Lexer lexer(source);
    auto lexResult = lexer.tokenize();
    Parser parser(lexResult.first);
    return parser.parse();
}

TEST(test_phrase_table_is_not_equal_to) {
    PhraseTable pt;
    std::vector<std::string> words = {"is", "not", "equal", "to"};
    auto result = pt.match(words, 0);
    ASSERT_TRUE(result.first);
    ASSERT_EQ(result.second.first, TokenType::IS_NOT_EQUAL_TO);
    ASSERT_EQ(result.second.second, 4);
}

TEST(test_phrase_table_is_at_least) {
    PhraseTable pt;
    std::vector<std::string> words = {"x", "is", "at", "least", "10"};
    auto result = pt.match(words, 1);
    ASSERT_TRUE(result.first);
    ASSERT_EQ(result.second.first, TokenType::IS_AT_LEAST);
    ASSERT_EQ(result.second.second, 3);
}

TEST(test_phrase_table_divided_by) {
    PhraseTable pt;
    std::vector<std::string> words = {"10", "divided", "by", "2"};
    auto result = pt.match(words, 1);
    ASSERT_TRUE(result.first);
    ASSERT_EQ(result.second.first, TokenType::DIVIDED_BY);
    ASSERT_EQ(result.second.second, 2);
}

TEST(test_phrase_table_keyword_set) {
    PhraseTable pt;
    std::vector<std::string> words = {"set", "x", "to", "5"};
    auto result = pt.match(words, 0);
    ASSERT_TRUE(result.first);
    ASSERT_EQ(result.second.first, TokenType::SET);
    ASSERT_EQ(result.second.second, 1);
}

TEST(test_phrase_table_unrecognized) {
    PhraseTable pt;
    std::vector<std::string> words = {"myVariable"};
    auto result = pt.match(words, 0);
    ASSERT_TRUE(!result.first);
}

TEST(test_phrase_table_greedy_longest) {
    PhraseTable pt;
    std::vector<std::string> words = {"x", "is", "not", "equal", "to", "y"};
    auto result = pt.match(words, 1);
    ASSERT_TRUE(result.first);
    ASSERT_EQ(result.second.first, TokenType::IS_NOT_EQUAL_TO);
    ASSERT_EQ(result.second.second, 4);
}

TEST(test_lexer_set_integer) {
    Lexer lexer("set x to 5\n");
    auto result = lexer.tokenize();
    ASSERT_TRUE(result.second.empty());
    ASSERT_EQ(result.first[0].type, TokenType::SET);
    ASSERT_EQ(result.first[1].type, TokenType::IDENTIFIER);
    ASSERT_EQ(result.first[1].value, std::string("x"));
    ASSERT_EQ(result.first[2].type, TokenType::TO);
    ASSERT_EQ(result.first[3].type, TokenType::NUMBER);
    ASSERT_EQ(result.first[4].type, TokenType::NEWLINE);
    ASSERT_EQ(result.first[5].type, TokenType::EOF_TOKEN);
}

TEST(test_lexer_set_decimal) {
    Lexer lexer("set pi to 3.14\n");
    auto result = lexer.tokenize();
    ASSERT_TRUE(result.second.empty());
    ASSERT_EQ(result.first[3].type, TokenType::DECIMAL);
}

TEST(test_lexer_set_string) {
    Lexer lexer("set name to \"Alice\"\n");
    auto result = lexer.tokenize();
    ASSERT_TRUE(result.second.empty());
    ASSERT_EQ(result.first[3].type, TokenType::STRING);
    ASSERT_EQ(result.first[3].value, std::string("Alice"));
}

TEST(test_lexer_comment_skipped) {
    Lexer lexer("note: this is a comment\n");
    auto result = lexer.tokenize();
    ASSERT_TRUE(result.second.empty());
    ASSERT_EQ(result.first[0].type, TokenType::EOF_TOKEN);
}

TEST(test_lexer_comparison_phrase) {
    Lexer lexer("if x is greater than 5 then\n");
    auto result = lexer.tokenize();
    ASSERT_TRUE(result.second.empty());
    bool found = false;
    for (const auto& tok : result.first) {
        if (tok.type == TokenType::IS_GREATER_THAN) found = true;
    }
    ASSERT_TRUE(found);
}

TEST(test_lexer_unexpected_char) {
    Lexer lexer("set x to 5 @ 3\n");
    auto result = lexer.tokenize();
    ASSERT_EQ((int)result.second.size(), 1);
}

TEST(test_lexer_rejects_out_of_range_numeric_literal_without_throwing) {
    Lexer lexer("set x to 999999999999999999999999999999999999999999999\n");
    auto result = lexer.tokenize();
    ASSERT_TRUE(!result.second.empty());
    ASSERT_TRUE(result.second[0].message.find("outside the supported range") != std::string::npos);
}

TEST(test_lexer_boolean_yes) {
    Lexer lexer("set flag to yes\n");
    auto result = lexer.tokenize();
    ASSERT_TRUE(result.second.empty());
    ASSERT_EQ(result.first[3].type, TokenType::YES);
}

TEST(test_lexer_parentheses) {
    Lexer lexer("say (x plus 1)\n");
    auto result = lexer.tokenize();
    ASSERT_TRUE(result.second.empty());
    bool foundL = false, foundR = false;
    for (const auto& tok : result.first) {
        if (tok.type == TokenType::LPAREN) foundL = true;
        if (tok.type == TokenType::RPAREN) foundR = true;
    }
    ASSERT_TRUE(foundL);
    ASSERT_TRUE(foundR);
}

TEST(test_parser_var_decl) {
    auto result = parseSource("set x to 5\n");
    ASSERT_TRUE(result.second.empty());
    ASSERT_EQ((int)result.first->statements.size(), 1);
    auto vd = std::dynamic_pointer_cast<VariableDeclaration>(result.first->statements[0]);
    ASSERT_TRUE(vd != nullptr);
    ASSERT_EQ(vd->name, std::string("x"));
}

TEST(test_parser_say_string) {
    auto result = parseSource("say \"hello\"\n");
    ASSERT_TRUE(result.second.empty());
    auto ps = std::dynamic_pointer_cast<PrintStatement>(result.first->statements[0]);
    ASSERT_TRUE(ps != nullptr);
}

TEST(test_parser_if_otherwise) {
    auto result = parseSource(
        "if x is equal to 10 then\n"
        "    say \"yes\"\n"
        "otherwise\n"
        "    say \"no\"\n"
        "end\n"
    );
    ASSERT_TRUE(result.second.empty());
    auto ifStmt = std::dynamic_pointer_cast<IfStatement>(result.first->statements[0]);
    ASSERT_TRUE(ifStmt != nullptr);
    ASSERT_EQ((int)ifStmt->thenBlock.size(), 1);
    ASSERT_TRUE(ifStmt->hasOtherwise);
    ASSERT_EQ((int)ifStmt->otherwiseBlock.size(), 1);
}

TEST(test_parser_arithmetic_precedence) {
    auto result = parseSource("say 2 plus 3 times 4\n");
    ASSERT_TRUE(result.second.empty());
    auto ps = std::dynamic_pointer_cast<PrintStatement>(result.first->statements[0]);
    auto be = std::dynamic_pointer_cast<BinaryExpression>(ps->expression);
    ASSERT_TRUE(be != nullptr);
    ASSERT_EQ(be->op, std::string("plus"));
    auto right = std::dynamic_pointer_cast<BinaryExpression>(be->right);
    ASSERT_TRUE(right != nullptr);
    ASSERT_EQ(right->op, std::string("times"));
}

TEST(test_parser_left_associativity) {
    auto result = parseSource("say 1 plus 2 plus 3\n");
    ASSERT_TRUE(result.second.empty());
    auto ps = std::dynamic_pointer_cast<PrintStatement>(result.first->statements[0]);
    auto be = std::dynamic_pointer_cast<BinaryExpression>(ps->expression);
    ASSERT_TRUE(be != nullptr);
    ASSERT_EQ(be->op, std::string("plus"));
    auto left = std::dynamic_pointer_cast<BinaryExpression>(be->left);
    ASSERT_TRUE(left != nullptr);
    ASSERT_EQ(left->op, std::string("plus"));
}

TEST(test_parser_boolean_logic) {
    auto result = parseSource(
        "if a is equal to 1 or b is equal to 2 and c is equal to 3 then\n"
        "    say a\n"
        "end\n"
    );
    ASSERT_TRUE(result.second.empty());
    auto ifStmt = std::dynamic_pointer_cast<IfStatement>(result.first->statements[0]);
    auto cond = std::dynamic_pointer_cast<LogicalExpression>(ifStmt->condition);
    ASSERT_TRUE(cond != nullptr);
    ASSERT_EQ(cond->op, std::string("or"));
    auto rightAnd = std::dynamic_pointer_cast<LogicalExpression>(cond->right);
    ASSERT_TRUE(rightAnd != nullptr);
    ASSERT_EQ(rightAnd->op, std::string("and"));
}

TEST(test_parser_not) {
    auto result = parseSource("if not x is equal to 5 then\n    say x\nend\n");
    ASSERT_TRUE(result.second.empty());
    auto ifStmt = std::dynamic_pointer_cast<IfStatement>(result.first->statements[0]);
    auto ue = std::dynamic_pointer_cast<UnaryExpression>(ifStmt->condition);
    ASSERT_TRUE(ue != nullptr);
    ASSERT_EQ(ue->op, std::string("not"));
}

TEST(test_parser_panic_recovery) {
    auto result = parseSource("set to 5\nsay \"hello\"\n");
    ASSERT_TRUE(result.second.size() >= 1);
    bool hasSay = false;
    for (const auto& s : result.first->statements) {
        if (std::dynamic_pointer_cast<PrintStatement>(s)) hasSay = true;
    }
    ASSERT_TRUE(hasSay);
}

TEST(test_symbol_table_declare_lookup) {
    SymbolTable st;
    auto entry = st.declare("x", "number", 1);
    ASSERT_TRUE(entry != nullptr);
    auto found = st.lookup("x");
    ASSERT_TRUE(found != nullptr);
    ASSERT_EQ(found->name, std::string("x"));
    ASSERT_EQ(found->type, std::string("number"));
}

TEST(test_symbol_table_scoped) {
    SymbolTable st;
    st.declare("x", "number", 1);
    st.pushScope();
    auto found = st.lookup("x");
    ASSERT_TRUE(found != nullptr);
    st.declare("y", "text", 3);
    st.popScope();
    ASSERT_TRUE(st.lookup("x") != nullptr);
    ASSERT_TRUE(st.lookup("y") == nullptr);
}

TEST(test_semantic_valid) {
    auto parseResult = parseSource("set x to 5\nset name to \"Alice\"\nsay x\n");
    SemanticAnalyzer analyzer;
    auto errors = analyzer.analyze(parseResult.first);
    ASSERT_TRUE(errors.empty());
}

TEST(test_semantic_rejects_return_outside_function) {
    auto parseResult = parseSource("give back 5\n");
    SemanticAnalyzer analyzer;
    auto errors = analyzer.analyze(parseResult.first);
    ASSERT_TRUE(!errors.empty());
    ASSERT_TRUE(errors[0].message.find("only valid inside a function") != std::string::npos);
}

TEST(test_semantic_rejects_non_boolean_condition) {
    auto parseResult = parseSource("if 1 then\n    say 2\nend\n");
    SemanticAnalyzer analyzer;
    auto errors = analyzer.analyze(parseResult.first);
    ASSERT_TRUE(!errors.empty());
    ASSERT_EQ(errors[0].category, std::string("TypeError"));
}

TEST(test_semantic_rejects_relational_text_comparison) {
    auto parseResult = parseSource("if \"a\" is less than \"b\" then\n    say 2\nend\n");
    SemanticAnalyzer analyzer;
    auto errors = analyzer.analyze(parseResult.first);
    ASSERT_TRUE(!errors.empty());
    ASSERT_EQ(errors[0].category, std::string("TypeError"));
}

TEST(test_semantic_undefined_var) {
    auto parseResult = parseSource("say x\n");
    SemanticAnalyzer analyzer;
    auto errors = analyzer.analyze(parseResult.first);
    ASSERT_EQ((int)errors.size(), 1);
    ASSERT_TRUE(errors[0].message.find("Undefined variable") != std::string::npos);
}

TEST(test_semantic_duplicate_decl) {
    auto parseResult = parseSource("set x to 5\nset x to 10\n");
    SemanticAnalyzer analyzer;
    auto errors = analyzer.analyze(parseResult.first);
    ASSERT_EQ((int)errors.size(), 1);
    ASSERT_TRUE(errors[0].message.find("already declared") != std::string::npos);
}

TEST(test_semantic_type_mismatch) {
    auto parseResult = parseSource("set x to 5\nset x to \"hello\"\n");
    SemanticAnalyzer analyzer;
    auto errors = analyzer.analyze(parseResult.first);
    ASSERT_EQ((int)errors.size(), 1);
    ASSERT_EQ(errors[0].category, std::string("TypeError"));
}

TEST(test_semantic_arithmetic_type) {
    auto parseResult = parseSource("set name to \"Alice\"\nsay name plus 1\n");
    SemanticAnalyzer analyzer;
    auto errors = analyzer.analyze(parseResult.first);
    ASSERT_TRUE(!errors.empty());
    bool hasTypeError = false;
    for (const auto& e : errors) {
        if (e.category == "TypeError") hasTypeError = true;
    }
    ASSERT_TRUE(hasTypeError);
}

TEST(test_semantic_scoped_visibility) {
    auto parseResult = parseSource(
        "set x to 5\n"
        "if x is greater than 0 then\n"
        "    set inner_var to 10\n"
        "end\n"
        "say inner_var\n"
    );
    SemanticAnalyzer analyzer;
    auto errors = analyzer.analyze(parseResult.first);
    bool hasUndefined = false;
    for (const auto& e : errors) {
        if (e.message.find("Undefined variable") != std::string::npos &&
            e.message.find("inner_var") != std::string::npos) {
            hasUndefined = true;
        }
    }
    ASSERT_TRUE(hasUndefined);
}

TEST(test_rejection_set_equal_to) {
    RejectionExplainer explainer;
    auto errors = explainer.explainLine("set age equal to 20", 1);
    ASSERT_EQ((int)errors.size(), 1);
    ASSERT_TRUE(errors[0].suggestion.find("set age to 20") != std::string::npos);
}

TEST(test_rejection_bad_comparison) {
    RejectionExplainer explainer;
    auto errors = explainer.explainLine("if name is old enough then", 2);
    ASSERT_EQ((int)errors.size(), 1);
    ASSERT_TRUE(errors[0].suggestion.find("is equal to") != std::string::npos);
}

TEST(test_rejection_print_suggests_say) {
    RejectionExplainer explainer;
    auto errors = explainer.explainLine("print x", 1);
    ASSERT_EQ((int)errors.size(), 1);
    ASSERT_TRUE(errors[0].suggestion.find("say") != std::string::npos);
}

TEST(test_rejection_else_suggests_otherwise) {
    RejectionExplainer explainer;
    auto errors = explainer.explainLine("else", 5);
    ASSERT_EQ((int)errors.size(), 1);
    ASSERT_TRUE(errors[0].suggestion.find("otherwise") != std::string::npos);
}

TEST(test_rejection_valid_no_error) {
    RejectionExplainer explainer;
    auto errors = explainer.explainLine("set x to 5", 1);
    ASSERT_TRUE(errors.empty());
}

TEST(test_pretty_print) {
    auto result = parseSource("set x to 5\nsay x\n");
    std::string output = result.first->prettyPrint();
    ASSERT_TRUE(output.find("Program") != std::string::npos);
    ASSERT_TRUE(output.find("VarDecl") != std::string::npos);
    ASSERT_TRUE(output.find("Say") != std::string::npos);
}

TEST(test_runtime_execute_basic) {
    auto result = parseSource("set x to 2\nset y to x plus 3\nsay y\n");
    Interpreter interpreter;
    auto runtime = interpreter.execute(result.first);
    ASSERT_TRUE(runtime.ok);
    ASSERT_EQ((int)runtime.output.size(), 1);
    ASSERT_TRUE(runtime.output[0] == "5");
}

TEST(test_runtime_function_call) {
    auto result = parseSource("create double using n\n    give back n plus n\nend\nset result to call double using 7\nsay result\n");
    Interpreter interpreter;
    auto runtime = interpreter.execute(result.first);
    ASSERT_TRUE(runtime.ok);
    ASSERT_TRUE(runtime.output[0] == "14");
}

TEST(test_runtime_function_call_with_multiple_parameters) {
    auto result = parseSource(
        "create combine using left, right, suffix\n"
        "    give back left plus right plus suffix\n"
        "end\n"
        "set answer to call combine using 2, 3, 4\n"
        "say answer\n");
    ASSERT_TRUE(result.second.empty());
    SemanticAnalyzer analyzer;
    ASSERT_TRUE(analyzer.analyze(result.first).empty());
    Interpreter interpreter;
    auto runtime = interpreter.execute(result.first);
    ASSERT_TRUE(runtime.ok);
    ASSERT_EQ(runtime.output[0], std::string("9"));
}

TEST(test_semantic_function_argument_count_and_return_inference) {
    auto result = parseSource(
        "create pair using left, right\n"
        "    give back left plus right\n"
        "end\n"
        "set answer to call pair using 2, 3\n");
    SemanticAnalyzer analyzer;
    ASSERT_TRUE(analyzer.analyze(result.first).empty());

    auto wrong = parseSource(
        "create pair using left, right\n"
        "    give back left plus right\n"
        "end\n"
        "set answer to call pair using 2\n");
    SemanticAnalyzer wrongAnalyzer;
    auto errors = wrongAnalyzer.analyze(wrong.first);
    ASSERT_TRUE(!errors.empty());
    ASSERT_TRUE(errors[0].message.find("expects 2 argument") != std::string::npos);
}

TEST(test_semantic_rejects_inconsistent_function_return_types) {
    auto result = parseSource(
        "create choose using flag\n"
        "    if flag is equal to yes then\n"
        "        give back 1\n"
        "    otherwise\n"
        "        give back \"text\"\n"
        "    end\n"
        "end\n");
    SemanticAnalyzer analyzer;
    auto errors = analyzer.analyze(result.first);
    bool found = false;
    for (const auto& error : errors) if (error.message.find("returns incompatible types") != std::string::npos) found = true;
    ASSERT_TRUE(found);
}

TEST(test_runtime_nested_multi_parameter_calls) {
    auto result = parseSource(
        "create add using left, right\n"
        "    give back left plus right\n"
        "end\n"
        "create twice using value\n"
        "    give back call add using value, value\n"
        "end\n"
        "set answer to call twice using 6\n"
        "say answer\n");
    SemanticAnalyzer analyzer;
    ASSERT_TRUE(analyzer.analyze(result.first).empty());
    Interpreter interpreter;
    auto runtime = interpreter.execute(result.first);
    ASSERT_TRUE(runtime.ok);
    ASSERT_EQ(runtime.output[0], std::string("12"));
}

TEST(test_runtime_propagates_return_from_nested_block) {
    const std::string source =
        "create choose using value\n"
        "    if value is greater than 0 then\n"
        "        give back value\n"
        "    end\n"
        "    give back 0\n"
        "end\n"
        "set answer to call choose using 7\n"
        "say answer\n";
    auto result = parseSource(source);
    ASSERT_TRUE(result.second.empty());
    SemanticAnalyzer analyzer;
    ASSERT_TRUE(analyzer.analyze(result.first).empty());
    Interpreter interpreter;
    auto runtime = interpreter.execute(result.first);
    ASSERT_TRUE(runtime.ok);
    ASSERT_EQ(runtime.output[0], std::string("7"));
}

TEST(test_runtime_preserves_decimal_arithmetic) {
    auto result = parseSource("say 98.5 plus 1\n");
    SemanticAnalyzer analyzer;
    ASSERT_TRUE(analyzer.analyze(result.first).empty());
    Interpreter interpreter;
    auto runtime = interpreter.execute(result.first);
    ASSERT_TRUE(runtime.ok);
    ASSERT_EQ(runtime.output[0], std::string("99.500000"));
}

TEST(test_runtime_rejects_integer_arithmetic_overflow) {
    auto result = parseSource("say 2147483647 plus 1\n");
    SemanticAnalyzer analyzer;
    ASSERT_TRUE(analyzer.analyze(result.first).empty());
    Interpreter interpreter;
    auto runtime = interpreter.execute(result.first);
    ASSERT_TRUE(!runtime.ok);
    ASSERT_TRUE(runtime.error.find("outside the supported range") != std::string::npos);
}

TEST(test_runtime_ask_uses_provided_text_input) {
    auto result = parseSource("ask name\nsay name\n");
    std::string requestedName;
    Interpreter interpreter([&requestedName](const std::string& name, std::string& value) {
        requestedName = name;
        value = "Ada Lovelace";
        return true;
    });
    auto runtime = interpreter.execute(result.first);
    ASSERT_TRUE(runtime.ok);
    ASSERT_TRUE(requestedName == "name");
    ASSERT_EQ((int)runtime.output.size(), 1);
    ASSERT_TRUE(runtime.output[0] == "Ada Lovelace");
    ASSERT_TRUE(runtime.events[0] == "ask name");
}

TEST(test_runtime_ask_reads_sequential_inputs) {
    auto result = parseSource("ask first\nask second\nsay first\nsay second\n");
    std::vector<std::string> values{"Ada", "Lovelace"};
    size_t next = 0;
    Interpreter interpreter([&values, &next](const std::string&, std::string& value) {
        if (next >= values.size()) return false;
        value = values[next++];
        return true;
    });
    auto runtime = interpreter.execute(result.first);
    ASSERT_TRUE(runtime.ok);
    ASSERT_EQ((int)runtime.output.size(), 2);
    ASSERT_TRUE(runtime.output[0] == "Ada");
    ASSERT_TRUE(runtime.output[1] == "Lovelace");
}

TEST(test_runtime_ask_accepts_empty_text) {
    auto result = parseSource("ask name\nsay name\n");
    Interpreter interpreter([](const std::string&, std::string& value) {
        value.clear();
        return true;
    });
    auto runtime = interpreter.execute(result.first);
    ASSERT_TRUE(runtime.ok);
    ASSERT_TRUE(runtime.output[0].empty());
}

TEST(test_runtime_ask_number_compares_age_threshold) {
    const std::string source =
        "ask age as number\n"
        "if age is at least 18 then\n"
        "    say \"adult\"\n"
        "otherwise\n"
        "    say \"minor\"\n"
        "end\n";
    const int ages[] = {17, 18, 21};
    const char* expected[] = {"minor", "adult", "adult"};
    for (int i = 0; i < 3; ++i) {
        auto result = parseSource(source);
        ASSERT_TRUE(result.second.empty());
        SemanticAnalyzer analyzer;
        ASSERT_TRUE(analyzer.analyze(result.first).empty());
        Interpreter interpreter([&ages, i](const std::string&, std::string& value) {
            value = std::to_string(ages[i]);
            return true;
        });
        auto runtime = interpreter.execute(result.first);
        ASSERT_TRUE(runtime.ok);
        ASSERT_EQ(runtime.output[0], expected[i]);
    }
}

TEST(test_runtime_ask_number_rejects_fractional_input) {
    auto result = parseSource("ask age as number\nsay age\n");
    Interpreter interpreter([](const std::string&, std::string& value) {
        value = "18.5";
        return true;
    });
    auto runtime = interpreter.execute(result.first);
    ASSERT_TRUE(!runtime.ok);
    ASSERT_TRUE(runtime.error.find("valid number") != std::string::npos);
}

TEST(test_runtime_ask_reports_end_of_input) {
    auto result = parseSource("ask name\nsay name\n");
    Interpreter interpreter([](const std::string&, std::string&) { return false; });
    auto runtime = interpreter.execute(result.first);
    ASSERT_TRUE(!runtime.ok);
    ASSERT_TRUE(runtime.error.find("Input ended") != std::string::npos);
}

TEST(test_json_report_escapes_strings) {
    std::string escaped = jsonEscape("\"hello\"\n\\test");
    ASSERT_TRUE(escaped.find("\\\"") != std::string::npos);
    ASSERT_TRUE(escaped.find("\\n") != std::string::npos);
    ASSERT_TRUE(escaped.find("\\\\") != std::string::npos);
}

TEST(test_json_report_serializes_ast_as_json) {
    const std::string source = "set x to 1\n";
    Lexer lexer(source);
    auto lexResult = lexer.tokenize();
    Parser parser(lexResult.first);
    auto parseResult = parser.parse();
    SemanticAnalyzer analyzer;
    auto semanticErrors = analyzer.analyze(parseResult.first);
    std::string report = buildCompilerReportJson(
        "test.lc", lexResult.first, lexResult.second, parseResult.second,
        semanticErrors, parseResult.first, analyzer.symbolTable.getAllSymbols(),
        nullptr, source);

    ASSERT_TRUE(report.find("\"schemaVersion\": \"1.1\"") != std::string::npos);
    ASSERT_TRUE(report.find("\"type\":\"VariableDeclaration\"") != std::string::npos);
    ASSERT_TRUE(report.find("\"ast\": {\"status\": \"available\"") != std::string::npos);
}

int main() {
    std::cout << "\n=== Lucid Compiler Test Suite ===\n\n";

    std::cout << "--- Phrase Table ---\n";
    RUN(test_phrase_table_is_not_equal_to);
    RUN(test_phrase_table_is_at_least);
    RUN(test_phrase_table_divided_by);
    RUN(test_phrase_table_keyword_set);
    RUN(test_phrase_table_unrecognized);
    RUN(test_phrase_table_greedy_longest);

    std::cout << "\n--- Lexer ---\n";
    RUN(test_lexer_set_integer);
    RUN(test_lexer_set_decimal);
    RUN(test_lexer_set_string);
    RUN(test_lexer_comment_skipped);
    RUN(test_lexer_comparison_phrase);
    RUN(test_lexer_unexpected_char);
    RUN(test_lexer_rejects_out_of_range_numeric_literal_without_throwing);
    RUN(test_lexer_boolean_yes);
    RUN(test_lexer_parentheses);

    std::cout << "\n--- Parser ---\n";
    RUN(test_parser_var_decl);
    RUN(test_parser_say_string);
    RUN(test_parser_if_otherwise);
    RUN(test_parser_arithmetic_precedence);
    RUN(test_parser_left_associativity);
    RUN(test_parser_boolean_logic);
    RUN(test_parser_not);
    RUN(test_parser_panic_recovery);

    std::cout << "\n--- Symbol Table ---\n";
    RUN(test_symbol_table_declare_lookup);
    RUN(test_symbol_table_scoped);

    std::cout << "\n--- Semantic Analyzer ---\n";
    RUN(test_semantic_valid);
    RUN(test_semantic_rejects_return_outside_function);
    RUN(test_semantic_rejects_non_boolean_condition);
    RUN(test_semantic_rejects_relational_text_comparison);
    RUN(test_semantic_undefined_var);
    RUN(test_semantic_duplicate_decl);
    RUN(test_semantic_type_mismatch);
    RUN(test_semantic_arithmetic_type);
    RUN(test_semantic_scoped_visibility);

    std::cout << "\n--- Rejection Explainer ---\n";
    RUN(test_rejection_set_equal_to);
    RUN(test_rejection_bad_comparison);
    RUN(test_rejection_print_suggests_say);
    RUN(test_rejection_else_suggests_otherwise);
    RUN(test_rejection_valid_no_error);

    std::cout << "\n--- AST Pretty Print ---\n";
    RUN(test_pretty_print);

    std::cout << "\n--- Runtime ---\n";
    RUN(test_runtime_execute_basic);
    RUN(test_runtime_function_call);
    RUN(test_runtime_function_call_with_multiple_parameters);
    RUN(test_semantic_function_argument_count_and_return_inference);
    RUN(test_semantic_rejects_inconsistent_function_return_types);
    RUN(test_runtime_nested_multi_parameter_calls);
    RUN(test_runtime_propagates_return_from_nested_block);
    RUN(test_runtime_preserves_decimal_arithmetic);
    RUN(test_runtime_rejects_integer_arithmetic_overflow);
    RUN(test_runtime_ask_uses_provided_text_input);
    RUN(test_runtime_ask_reads_sequential_inputs);
    RUN(test_runtime_ask_accepts_empty_text);
    RUN(test_runtime_ask_number_compares_age_threshold);
    RUN(test_runtime_ask_number_rejects_fractional_input);
    RUN(test_runtime_ask_reports_end_of_input);

    std::cout << "\n--- JSON report ---\n";
    RUN(test_json_report_escapes_strings);
    RUN(test_json_report_serializes_ast_as_json);

    std::cout << "\n=== Results: " << testsPassed << " passed, " << testsFailed << " failed ===\n\n";

    return testsFailed > 0 ? 1 : 0;
}
