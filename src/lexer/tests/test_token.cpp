#include <iostream>
#include <string>
#include <vector>
#include <cstring>
#include "token.hpp"

using namespace sql;

static int passed = 0;
static int failed = 0;

#define CHECK(name, cond) do { \
    if (cond) { \
        ++passed; \
        std::cout << "[OK]   " << name << "\n"; \
    } else { \
        ++failed; \
        std::cout << "[FAIL] " << name << " (linea " << __LINE__ << ")\n"; \
    } \
} while (0)

#define CHECK_STR_EQ(name, actual, expected) do { \
    if (std::string(actual) == std::string(expected)) { \
        ++passed; \
        std::cout << "[OK]   " << name << "\n"; \
    } else { \
        ++failed; \
        std::cout << "[FAIL] " << name << " -> esperado: \"" << (expected) \
                  << "\", obtenido: \"" << (actual) << "\" (linea " << __LINE__ << ")\n"; \
    } \
} while (0)

int main() {
    std::cout << "=== Pruebas Unitarias para token.cpp / token.hpp ===\n\n";

    // 1. Palabras reservadas
    std::cout << "-- 1. Palabras Reservadas --\n";
    CHECK_STR_EQ("KEYWORD_SELECT -> \"KEYWORD_SELECT\"", tokenTypeName(TokenType::KEYWORD_SELECT), "KEYWORD_SELECT");
    CHECK_STR_EQ("KEYWORD_FROM   -> \"KEYWORD_FROM\"",   tokenTypeName(TokenType::KEYWORD_FROM),   "KEYWORD_FROM");
    CHECK_STR_EQ("KEYWORD_WHERE  -> \"KEYWORD_WHERE\"",  tokenTypeName(TokenType::KEYWORD_WHERE),  "KEYWORD_WHERE");
    CHECK_STR_EQ("KEYWORD_AND    -> \"KEYWORD_AND\"",    tokenTypeName(TokenType::KEYWORD_AND),    "KEYWORD_AND");
    CHECK_STR_EQ("KEYWORD_OR     -> \"KEYWORD_OR\"",     tokenTypeName(TokenType::KEYWORD_OR),     "KEYWORD_OR");

    // 2. Identificadores y Literales
    std::cout << "\n-- 2. Identificadores y Literales --\n";
    CHECK_STR_EQ("IDENTIFIER      -> \"IDENTIFIER\"",      tokenTypeName(TokenType::IDENTIFIER),      "IDENTIFIER");
    CHECK_STR_EQ("INTEGER_LITERAL -> \"INTEGER_LITERAL\"", tokenTypeName(TokenType::INTEGER_LITERAL), "INTEGER_LITERAL");
    CHECK_STR_EQ("DECIMAL_LITERAL -> \"DECIMAL_LITERAL\"", tokenTypeName(TokenType::DECIMAL_LITERAL), "DECIMAL_LITERAL");
    CHECK_STR_EQ("STRING_LITERAL  -> \"STRING_LITERAL\"",  tokenTypeName(TokenType::STRING_LITERAL),  "STRING_LITERAL");

    // 3. Operadores Relacionales
    std::cout << "\n-- 3. Operadores Relacionales --\n";
    CHECK_STR_EQ("EQUAL         -> \"EQUAL\"",         tokenTypeName(TokenType::EQUAL),         "EQUAL");
    CHECK_STR_EQ("NOT_EQUAL     -> \"NOT_EQUAL\"",     tokenTypeName(TokenType::NOT_EQUAL),     "NOT_EQUAL");
    CHECK_STR_EQ("LESS          -> \"LESS\"",          tokenTypeName(TokenType::LESS),          "LESS");
    CHECK_STR_EQ("GREATER       -> \"GREATER\"",       tokenTypeName(TokenType::GREATER),       "GREATER");
    CHECK_STR_EQ("LESS_EQUAL    -> \"LESS_EQUAL\"",    tokenTypeName(TokenType::LESS_EQUAL),    "LESS_EQUAL");
    CHECK_STR_EQ("GREATER_EQUAL -> \"GREATER_EQUAL\"", tokenTypeName(TokenType::GREATER_EQUAL), "GREATER_EQUAL");

    // 4. Puntuación y Extensiones
    std::cout << "\n-- 4. Puntuacion y Extensiones --\n";
    CHECK_STR_EQ("COMMA     -> \"COMMA\"",     tokenTypeName(TokenType::COMMA),     "COMMA");
    CHECK_STR_EQ("SEMICOLON -> \"SEMICOLON\"", tokenTypeName(TokenType::SEMICOLON), "SEMICOLON");
    CHECK_STR_EQ("STAR      -> \"STAR\"",      tokenTypeName(TokenType::STAR),      "STAR");
    CHECK_STR_EQ("LPAREN    -> \"LPAREN\"",    tokenTypeName(TokenType::LPAREN),    "LPAREN");
    CHECK_STR_EQ("RPAREN    -> \"RPAREN\"",    tokenTypeName(TokenType::RPAREN),    "RPAREN");

    // 5. Fin de Entrada
    std::cout << "\n-- 5. Fin de Entrada --\n";
    CHECK_STR_EQ("END_OF_FILE -> \"EOF\"", tokenTypeName(TokenType::END_OF_FILE), "EOF");

    // 6. Valores no reconocidos / desconocidos
    std::cout << "\n-- 6. Valores Desconocidos --\n";
    CHECK_STR_EQ("Valor invalido 999 -> \"UNKNOWN\"", tokenTypeName(static_cast<TokenType>(999)), "UNKNOWN");
    CHECK_STR_EQ("Valor invalido -1  -> \"UNKNOWN\"", tokenTypeName(static_cast<TokenType>(-1)),  "UNKNOWN");

    // 7. Estructura Token
    std::cout << "\n-- 7. Estructura Token --\n";
    {
        Token t{TokenType::KEYWORD_SELECT, "SELECT", 1, 1};
        CHECK("Token struct: tipo correcto", t.type == TokenType::KEYWORD_SELECT);
        CHECK("Token struct: lexema correcto", t.lexeme == "SELECT");
        CHECK("Token struct: linea 1", t.line == 1);
        CHECK("Token struct: columna 1", t.column == 1);
    }
    {
        Token t{TokenType::STRING_LITERAL, "cadena con espacios y simbolos <>=", 10, 25};
        CHECK("Token con lexema compuesto", t.lexeme == "cadena con espacios y simbolos <>=");
        CHECK("Token posicion linea 10 col 25", t.line == 10 && t.column == 25);
    }
    {
        Token t{TokenType::END_OF_FILE, "", 42, 100};
        CHECK("Token EOF con lexema vacio", t.lexeme.empty());
        CHECK("Token EOF posicion", t.line == 42 && t.column == 100);
    }
    {
        // Copia y movimiento
        Token t1{TokenType::IDENTIFIER, "mi_variable", 3, 7};
        Token t2 = t1; // copia
        CHECK("Copia de Token: lexema igual", t2.lexeme == "mi_variable");
        CHECK("Copia de Token: tipo igual", t2.type == TokenType::IDENTIFIER);

        Token t3 = std::move(t1); // movimiento
        CHECK("Movimiento de Token: lexema en destino", t3.lexeme == "mi_variable");
    }
    {
        // Vector de tokens
        std::vector<Token> tokens;
        tokens.push_back({TokenType::KEYWORD_SELECT, "SELECT", 1, 1});
        tokens.push_back({TokenType::IDENTIFIER, "nombre", 1, 8});
        tokens.push_back({TokenType::END_OF_FILE, "", 1, 14});

        CHECK("Vector de tokens: size 3", tokens.size() == 3);
        CHECK("Vector tokens[0]: KEYWORD_SELECT", tokens[0].type == TokenType::KEYWORD_SELECT);
        CHECK("Vector tokens[1]: IDENTIFIER", tokens[1].type == TokenType::IDENTIFIER);
        CHECK("Vector tokens[2]: END_OF_FILE", tokens[2].type == TokenType::END_OF_FILE);
    }

    std::cout << "\n=========================================\n";
    std::cout << "Resumen: " << passed << " OK, " << failed << " FAIL\n";
    std::cout << "=========================================\n";

    return (failed == 0) ? 0 : 1;
}
