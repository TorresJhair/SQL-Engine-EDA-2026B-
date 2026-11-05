#pragma once
#include <string>
#include <vector>
#include "token.hpp"

namespace sql {

struct LexError {
    std::string message;
    std::string lexeme;  // secuencia ofensiva
    int line;
    int column;
};

struct LexResult {
    std::vector<Token>    tokens;  // siempre termina en END_OF_FILE
    std::vector<LexError> errors;  // errores léxicos (NO son tokens)
    bool ok() const { return errors.empty(); }
};

class Lexer {
public:
    explicit Lexer(std::string source);
    LexResult tokenize();

private:
    std::string src_;
    size_t pos_ = 0;
    int line_ = 1, col_ = 1;
    LexResult out_;

    bool   eof() const { return pos_ >= src_.size(); }
    char   peek(size_t off = 0) const;
    char   advance();
    void   skipWhitespace();
    void   emit(TokenType t, std::string lexeme, int line, int col);
    void   error(std::string msg, std::string lexeme, int line, int col);

    void scanWord(int line, int col);
    void scanNumber(int line, int col);
    void scanString(int line, int col);
    void scanOperator(int line, int col);
};

}  // namespace sql
