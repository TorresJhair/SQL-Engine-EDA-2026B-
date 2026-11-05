#include "lexer.hpp"
#include <cctype>
#include <unordered_map>
#include <utility>

namespace sql {

namespace {
// Tabla de palabras reservadas (clave en MAYÚSCULAS).
const std::unordered_map<std::string, TokenType>& keywords() {
    static const std::unordered_map<std::string, TokenType> k = {
        {"SELECT", TokenType::KEYWORD_SELECT}, {"FROM", TokenType::KEYWORD_FROM},
        {"WHERE",  TokenType::KEYWORD_WHERE},  {"AND",  TokenType::KEYWORD_AND},
        {"OR",     TokenType::KEYWORD_OR}};
    return k;
}
bool isLetter(char c)  { return std::isalpha(static_cast<unsigned char>(c)) || c == '_'; }
bool isDigit(char c)   { return std::isdigit(static_cast<unsigned char>(c)) != 0; }
bool isAlnum_(char c)  { return isLetter(c) || isDigit(c); }
bool isNonAscii(char c) { return static_cast<unsigned char>(c) >= 0x80; }
bool isWordByte(char c) { return isAlnum_(c) || isNonAscii(c); }
}  // namespace

Lexer::Lexer(std::string source) : src_(std::move(source)) {}

char Lexer::peek(size_t off) const {
    return pos_ + off < src_.size() ? src_[pos_ + off] : '\0';
}

char Lexer::advance() {
    char c = src_[pos_++];
    if (c == '\n') { ++line_; col_ = 1; }
    else if ((static_cast<unsigned char>(c) & 0xC0) != 0x80) ++col_;  // bytes de continuación UTF-8 no cuentan
    return c;
}

void Lexer::skipWhitespace() {
    while (!eof() && std::isspace(static_cast<unsigned char>(peek()))) advance();
}

void Lexer::emit(TokenType t, std::string lexeme, int line, int col) {
    out_.tokens.push_back({t, std::move(lexeme), line, col});
}

void Lexer::error(std::string msg, std::string lexeme, int line, int col) {
    out_.errors.push_back({std::move(msg), std::move(lexeme), line, col});
}

LexResult Lexer::tokenize() {
    while (true) {
        skipWhitespace();
        if (eof()) break;
        int line = line_, col = col_;
        char c = peek();

        // Una sola cadena if / else-if: cada carácter se despacha exactamente una vez.
        if (isLetter(c) || isNonAscii(c))
                               scanWord(line, col);
        else if (isDigit(c) || ((c == '+' || c == '-') && isDigit(peek(1))))
                               scanNumber(line, col);
        else if (c == '\'')    scanString(line, col);
        else if (c == ',')   { advance(); emit(TokenType::COMMA,     ",", line, col); }
        else if (c == ';')   { advance(); emit(TokenType::SEMICOLON, ";", line, col); }
        else if (c == '*')   { advance(); emit(TokenType::STAR,      "*", line, col); }
        else if (c == '(')   { advance(); emit(TokenType::LPAREN,    "(", line, col); }
        else if (c == ')')   { advance(); emit(TokenType::RPAREN,    ")", line, col); }
        else if (c == '=' || c == '<' || c == '>' || c == '!') scanOperator(line, col);
        else {
            // Carácter no admitido: se consume completo (incluye secuencias UTF-8 multibyte).
            std::string bad(1, advance());
            while (!eof() && (static_cast<unsigned char>(peek()) & 0xC0) == 0x80) bad += advance();
            error("caracter no admitido", bad, line, col);
        }
    }
    emit(TokenType::END_OF_FILE, "", line_, col_);
    return std::move(out_);
}

void Lexer::scanWord(int line, int col) {
    std::string w;
    bool invalid = false;
    while (isWordByte(peek())) {
        if (isNonAscii(peek())) invalid = true;
        w += advance();
    }

    if (invalid) {
        error("identificador contiene caracteres no ASCII", w, line, col);
        return;
    }

    std::string up;
    for (char ch : w) up += static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    auto it = keywords().find(up);
    emit(it != keywords().end() ? it->second : TokenType::IDENTIFIER, w, line, col);
}

void Lexer::scanNumber(int line, int col) {
    std::string n;
    if (peek() == '+' || peek() == '-') n += advance();
    while (isDigit(peek())) n += advance();
    bool decimal = false, exponent = false, bad = false;
    std::string why;

    // Parte fraccionaria
    if (peek() == '.') {
        if (isDigit(peek(1))) {
            decimal = true;
            n += advance();
            while (isDigit(peek())) n += advance();
        } else {
            n += advance();  // '12.' sin dígitos fraccionarios
            bad = true;
            why = "numero decimal mal formado (faltan digitos tras el punto)";
        }
    }

    // Notación científica
    if (peek() == 'e' || peek() == 'E') {
        exponent = true;
        n += advance();
        if (peek() == '+' || peek() == '-') n += advance();

        if (!isDigit(peek())) {
            bad = true;
            why = "notacion cientifica mal formada (faltan digitos en el exponente)";
        } else {
            while (isDigit(peek())) n += advance();
        }
    }

    // Letras o bytes no ASCII pegados al número: '12abc', '3.5x', '12niño'
    if (isLetter(peek()) || isNonAscii(peek())) {
        while (isWordByte(peek())) n += advance();
        bad = true;
        if (why.empty()) why = "numero seguido de letras (identificador mal formado)";
    }

    if (bad) error(why, n, line, col);
    else emit(decimal || exponent ? TokenType::DECIMAL_LITERAL : TokenType::INTEGER_LITERAL,
              n, line, col);
}

void Lexer::scanString(int line, int col) {
    std::string raw = "'", value;
    advance();  // comilla de apertura
    while (!eof()) {
        char c = advance();
        raw += c;
        if (c == '\'') {
            if (peek() == '\'') {  // '' => comilla escapada
                value += '\'';
                raw += advance();
                continue;
            }
            emit(TokenType::STRING_LITERAL, value, line, col);
            return;
        }
        value += c;
    }
    error("cadena sin cerrar (falta comilla simple de cierre)", raw, line, col);
}

void Lexer::scanOperator(int line, int col) {
    char c = advance();
    char n = peek();
    switch (c) {
        case '=': emit(TokenType::EQUAL, "=", line, col); return;
        case '<':
            if (n == '=') { advance(); emit(TokenType::LESS_EQUAL, "<=", line, col); }
            else if (n == '>') { advance(); emit(TokenType::NOT_EQUAL, "<>", line, col); }
            else emit(TokenType::LESS, "<", line, col);
            return;
        case '>':
            if (n == '=') { advance(); emit(TokenType::GREATER_EQUAL, ">=", line, col); }
            else emit(TokenType::GREATER, ">", line, col);
            return;
        case '!':
            if (n == '=') { advance(); emit(TokenType::NOT_EQUAL, "!=", line, col); }
            else error("operador incompleto: se esperaba '!='", "!", line, col);
            return;
    }
}

}  // namespace sql