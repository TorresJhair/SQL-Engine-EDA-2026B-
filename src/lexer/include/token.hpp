#pragma once
#include <string>

namespace sql {

enum class TokenType {
    // Palabras reservadas
    KEYWORD_SELECT, KEYWORD_FROM, KEYWORD_WHERE, KEYWORD_AND, KEYWORD_OR,
    // Identificadores y literales
    IDENTIFIER, INTEGER_LITERAL, DECIMAL_LITERAL, STRING_LITERAL,
    // Operadores relacionales
    EQUAL, NOT_EQUAL, LESS, GREATER, LESS_EQUAL, GREATER_EQUAL,
    // Puntuación y extensiones
    COMMA, SEMICOLON, STAR, LPAREN, RPAREN,
    // Fin de entrada
    END_OF_FILE
};

struct Token {
    TokenType   type;
    std::string lexeme;  // texto reconocido
    int         line;    // donde comienza el token
    int         column;  // donde comienza el token
};

const char* tokenTypeName(TokenType t);

}  // namespace sql