#include "token.hpp"

namespace sql {
const char* tokenTypeName(TokenType t) {
    switch (t) {
        case TokenType::KEYWORD_SELECT:  return "KEYWORD_SELECT";
        case TokenType::KEYWORD_FROM:    return "KEYWORD_FROM";
        case TokenType::KEYWORD_WHERE:   return "KEYWORD_WHERE";
        case TokenType::KEYWORD_AND:     return "KEYWORD_AND";
        case TokenType::KEYWORD_OR:      return "KEYWORD_OR";
        case TokenType::IDENTIFIER:      return "IDENTIFIER";
        case TokenType::INTEGER_LITERAL: return "INTEGER_LITERAL";
        case TokenType::DECIMAL_LITERAL: return "DECIMAL_LITERAL";
        case TokenType::STRING_LITERAL:  return "STRING_LITERAL";
        case TokenType::EQUAL:           return "EQUAL";
        case TokenType::NOT_EQUAL:       return "NOT_EQUAL";
        case TokenType::LESS:            return "LESS";
        case TokenType::GREATER:         return "GREATER";
        case TokenType::LESS_EQUAL:      return "LESS_EQUAL";
        case TokenType::GREATER_EQUAL:   return "GREATER_EQUAL";
        case TokenType::COMMA:           return "COMMA";
        case TokenType::SEMICOLON:       return "SEMICOLON";
        case TokenType::STAR:            return "STAR";
        case TokenType::LPAREN:          return "LPAREN";
        case TokenType::RPAREN:          return "RPAREN";
        case TokenType::END_OF_FILE:     return "EOF";
    }
    return "UNKNOWN";
}
}  // namespace sql