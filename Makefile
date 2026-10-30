CXX = g++
CXXFLAGS = -std=c++14 -Wall -Wextra -O2
SRC_DIR = src
BUILD_DIR = build

SOURCES = \
	$(SRC_DIR)/lexer/phrase_table.cpp \
	$(SRC_DIR)/lexer/lexer.cpp \
	$(SRC_DIR)/parser/parser.cpp \
	$(SRC_DIR)/semantic/symbol_table.cpp \
	$(SRC_DIR)/semantic/analyzer.cpp \
	$(SRC_DIR)/errors/error_types.cpp \
	$(SRC_DIR)/errors/rejection_explainer.cpp \
	$(SRC_DIR)/runtime/interpreter.cpp \
	$(SRC_DIR)/runtime/compiler_report.cpp

MAIN_SRC = $(SRC_DIR)/main.cpp
TEST_SRC = tests/test_main.cpp

TARGET = lucid.exe
TEST_TARGET = test_lucid.exe

all: $(TARGET)

$(TARGET): $(SOURCES) $(MAIN_SRC)
	$(CXX) $(CXXFLAGS) -I$(SRC_DIR) -o $(TARGET) $(MAIN_SRC) $(SOURCES)

test: $(TEST_TARGET)
	./$(TEST_TARGET)

$(TEST_TARGET): $(SOURCES) $(TEST_SRC)
	$(CXX) $(CXXFLAGS) -I$(SRC_DIR) -o $(TEST_TARGET) $(TEST_SRC) $(SOURCES)

clean:
	del /f $(TARGET) $(TEST_TARGET) 2>nul

.PHONY: all test clean
