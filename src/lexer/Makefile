CXX      := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -Wpedantic -Iinclude
SOURCES  := src/token.cpp src/lexer.cpp

.PHONY: all test clean

all: lexer test_lexer test_token

lexer: $(SOURCES) src/main.cpp
	$(CXX) $(CXXFLAGS) $^ -o $@

test_lexer: $(SOURCES) tests/test_lexer.cpp
	$(CXX) $(CXXFLAGS) $^ -o $@

test_token: src/token.cpp tests/test_token.cpp
	$(CXX) $(CXXFLAGS) $^ -o $@

test: test_lexer test_token
	./test_lexer
	./test_token

clean:
	rm -f lexer test_lexer test_token
