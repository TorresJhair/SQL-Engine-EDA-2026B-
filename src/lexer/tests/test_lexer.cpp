#include <iostream>
#include <string>
#include <vector>

#include "lexer.hpp"

using sql::LexResult;
using sql::Lexer;
using T = sql::TokenType;

static int passed = 0;
static int failed = 0;

#define CHECK(name, condition) do {                                      \
    if (condition) {                                                     \
        ++passed;                                                        \
        std::cout << "[OK]   " << name << '\n';                         \
    } else {                                                            \
        ++failed;                                                        \
        std::cout << "[FAIL] " << name << " (linea " << __LINE__       \
                  << ")\n";                                             \
    }                                                                   \
} while (false)

static LexResult lex(const std::string& source) {
    return Lexer(source).tokenize();
}

static std::vector<T> types(const LexResult& result) {
    std::vector<T> output;
    for (const auto& token : result.tokens) {
        output.push_back(token.type);
    }
    return output;
}

int main() {
    {
        auto result = lex("SELECT nombre, edad\nFROM estudiantes\nWHERE edad >= 18;");
        const std::vector<T> expected = {
            T::KEYWORD_SELECT, T::IDENTIFIER, T::COMMA, T::IDENTIFIER,
            T::KEYWORD_FROM, T::IDENTIFIER, T::KEYWORD_WHERE, T::IDENTIFIER,
            T::GREATER_EQUAL, T::INTEGER_LITERAL, T::SEMICOLON,
            T::END_OF_FILE
        };
        CHECK("1. consulta valida: secuencia de tipos", types(result) == expected);
        CHECK("1. consulta valida: sin errores", result.ok());
        CHECK("1. posicion de FROM = 2:1",
              result.tokens[4].line == 2 && result.tokens[4].column == 1);
        CHECK("1. posicion de 18 = 3:15",
              result.tokens[9].line == 3 && result.tokens[9].column == 15);
    }

    {
        auto result = lex(">= <= <> != = < >");
        const std::vector<T> expected = {
            T::GREATER_EQUAL, T::LESS_EQUAL, T::NOT_EQUAL, T::NOT_EQUAL,
            T::EQUAL, T::LESS, T::GREATER, T::END_OF_FILE
        };
        CHECK("2. operadores simples y compuestos", types(result) == expected);

        auto joined = lex("a>=b");
        CHECK("2. 'a>=b' sin espacios",
              types(joined) == std::vector<T>({T::IDENTIFIER, T::GREATER_EQUAL,
                                               T::IDENTIFIER, T::END_OF_FILE}));

        auto separated = lex("< >");
        CHECK("2. '< >' separados no forman <>",
              types(separated) == std::vector<T>({T::LESS, T::GREATER,
                                                  T::END_OF_FILE}));
    }

    {
        auto result = lex("42 3.14 'hola mundo' 0 007");
        CHECK("3. entero", result.tokens[0].type == T::INTEGER_LITERAL &&
                           result.tokens[0].lexeme == "42");
        CHECK("3. decimal", result.tokens[1].type == T::DECIMAL_LITERAL &&
                            result.tokens[1].lexeme == "3.14");
        CHECK("3. cadena sin comillas en lexema",
              result.tokens[2].type == T::STRING_LITERAL &&
              result.tokens[2].lexeme == "hola mundo");
        CHECK("3. ceros a la izquierda",
              result.tokens[4].type == T::INTEGER_LITERAL &&
              result.tokens[4].lexeme == "007");

        auto escaped = lex("'it''s'");
        CHECK("3. comilla escapada ''",
              escaped.tokens[0].lexeme == "it's" && escaped.ok());

        auto empty = lex("''");
        CHECK("3. cadena vacia", empty.tokens[0].type == T::STRING_LITERAL &&
                                  empty.tokens[0].lexeme.empty());

        auto keywords = lex("'SELECT FROM ; >='");
        CHECK("3. palabras reservadas dentro de cadena no se tokenizan",
              keywords.tokens.size() == 2);

        auto signedNumbers = lex("-42 +7 -3.14 +0.5");
        CHECK("3. enteros con signo",
              signedNumbers.tokens[0].type == T::INTEGER_LITERAL &&
              signedNumbers.tokens[0].lexeme == "-42" &&
              signedNumbers.tokens[1].type == T::INTEGER_LITERAL &&
              signedNumbers.tokens[1].lexeme == "+7");
        CHECK("3. decimales con signo",
              signedNumbers.tokens[2].type == T::DECIMAL_LITERAL &&
              signedNumbers.tokens[2].lexeme == "-3.14" &&
              signedNumbers.tokens[3].type == T::DECIMAL_LITERAL &&
              signedNumbers.tokens[3].lexeme == "+0.5" && signedNumbers.ok());

        auto scientific = lex("1e5 1e-10 2.5E+3 -4E2 +6.02e23");
        CHECK("3. notacion cientifica",
              types(scientific) == std::vector<T>({
                  T::DECIMAL_LITERAL, T::DECIMAL_LITERAL, T::DECIMAL_LITERAL,
                  T::DECIMAL_LITERAL, T::DECIMAL_LITERAL, T::END_OF_FILE
              }) && scientific.ok());
        CHECK("3. notacion cientifica conserva el lexema",
              scientific.tokens[1].lexeme == "1e-10" &&
              scientific.tokens[2].lexeme == "2.5E+3" &&
              scientific.tokens[3].lexeme == "-4E2" &&
              scientific.tokens[4].lexeme == "+6.02e23");
    }

    {
        auto result = lex("select Select SELECT selecto from_x _tabla t1 AND or");
        const std::vector<T> expected = {
            T::KEYWORD_SELECT, T::KEYWORD_SELECT, T::KEYWORD_SELECT,
            T::IDENTIFIER, T::IDENTIFIER, T::IDENTIFIER, T::IDENTIFIER,
            T::KEYWORD_AND, T::KEYWORD_OR, T::END_OF_FILE
        };
        CHECK("4. keywords sin distinguir mayusculas y prefijos identificadores",
              types(result) == expected);
        CHECK("4. lexema conserva escritura original",
              result.tokens[1].lexeme == "Select");

        auto nonAscii = lex("SELECT diñosáurio FROM tabla;");
        CHECK("4. identificador no ASCII completo genera un error",
              nonAscii.errors.size() == 1 &&
              nonAscii.errors[0].lexeme == "diñosáurio" &&
              nonAscii.errors[0].line == 1 &&
              nonAscii.errors[0].column == 8);
        CHECK("4. partes del identificador invalido no generan tokens",
              types(nonAscii) == std::vector<T>({
                  T::KEYWORD_SELECT, T::KEYWORD_FROM, T::IDENTIFIER,
                  T::SEMICOLON, T::END_OF_FILE
              }) && nonAscii.tokens[2].lexeme == "tabla");

        auto positions = lex("ñandu arbolá válido_2 normal");
        CHECK("4. caracteres no ASCII en cualquier posicion invalidan la palabra",
              positions.errors.size() == 3 &&
              positions.errors[0].lexeme == "ñandu" &&
              positions.errors[1].lexeme == "arbolá" &&
              positions.errors[2].lexeme == "válido_2");
        CHECK("4. identificadores ASCII posteriores se conservan",
              types(positions) == std::vector<T>({T::IDENTIFIER,
                                                  T::END_OF_FILE}) &&
              positions.tokens[0].lexeme == "normal");
    }

    {
        auto result = lex("  \t SELECT \r\n\n\t a  ");
        CHECK("5. espacios, tabuladores y saltos ignorados",
              types(result) == std::vector<T>({T::KEYWORD_SELECT, T::IDENTIFIER,
                                               T::END_OF_FILE}));
        CHECK("5. linea y columna tras saltos",
              result.tokens[1].line == 3 && result.tokens[1].column == 3);
    }

    {
        auto invalid = lex("SELECT a # b");
        CHECK("6. caracter invalido reportado",
              invalid.errors.size() == 1 && invalid.errors[0].lexeme == "#" &&
              invalid.errors[0].column == 10);
        CHECK("6. caracter invalido no es token y el analisis continua",
              types(invalid) == std::vector<T>({T::KEYWORD_SELECT, T::IDENTIFIER,
                                                T::IDENTIFIER, T::END_OF_FILE}));

        auto unclosed = lex("WHERE n = 'abc");
        CHECK("6. cadena sin cerrar",
              unclosed.errors.size() == 1 && unclosed.errors[0].column == 11 &&
              unclosed.tokens.back().type == T::END_OF_FILE);
        CHECK("6. cadena sin cerrar no genera literal",
              types(unclosed) == std::vector<T>({T::KEYWORD_WHERE, T::IDENTIFIER,
                                                 T::EQUAL, T::END_OF_FILE}));

        auto identifier = lex("12abc");
        CHECK("6. numero seguido de letras es error",
              identifier.errors.size() == 1 && identifier.tokens.size() == 1);

        auto numberAndNonAscii = lex("12niño");
        CHECK("6. numero seguido de palabra no ASCII es un solo error",
              numberAndNonAscii.errors.size() == 1 &&
              numberAndNonAscii.errors[0].lexeme == "12niño" &&
              numberAndNonAscii.tokens.size() == 1);

        auto decimal = lex("12.");
        CHECK("6. decimal sin fraccion es error",
              decimal.errors.size() == 1 && decimal.tokens.size() == 1);

        auto bang = lex("a ! b");
        CHECK("6. signo ! aislado es error", bang.errors.size() == 1);

        auto utf8 = lex("SELECT ñ");
        CHECK("6. caracter UTF-8 genera un solo error",
              utf8.errors.size() == 1 && utf8.errors[0].lexeme == "ñ");

        auto several = lex("@ $\n%");
        CHECK("6. varios errores conservan posicion",
              several.errors.size() == 3 && several.errors[2].line == 2 &&
              several.errors[2].column == 1);

        auto exponents = lex("1e 1e+ 1e-");
        CHECK("6. exponentes incompletos son errores",
              exponents.errors.size() == 3 && exponents.tokens.size() == 1 &&
              exponents.errors[0].lexeme == "1e" &&
              exponents.errors[1].lexeme == "1e+" &&
              exponents.errors[2].lexeme == "1e-");

        auto signedInvalid = lex("-12abc +3.5x");
        CHECK("6. numeros signados mal formados son errores completos",
              signedInvalid.errors.size() == 2 &&
              signedInvalid.errors[0].lexeme == "-12abc" &&
              signedInvalid.errors[1].lexeme == "+3.5x" &&
              signedInvalid.tokens.size() == 1);
    }

    {
        auto empty = lex("");
        CHECK("7. entrada vacia produce solo EOF",
              empty.tokens.size() == 1 &&
              empty.tokens[0].type == T::END_OF_FILE && empty.ok());

        auto whitespace = lex("   \n  ");
        CHECK("7. solo espacios produce solo EOF", whitespace.tokens.size() == 1);

        auto incomplete = lex("SELECT");
        CHECK("7. sentencia incompleta es lexicalmente valida",
              types(incomplete) == std::vector<T>({T::KEYWORD_SELECT,
                                                   T::END_OF_FILE}) &&
              incomplete.ok());

        auto op = lex("a>=");
        CHECK("7. operador al final",
              types(op) == std::vector<T>({T::IDENTIFIER, T::GREATER_EQUAL,
                                           T::END_OF_FILE}));

        auto number = lex("x=5");
        CHECK("7. numero al final sin espacio",
              number.tokens[2].lexeme == "5" &&
              number.tokens[2].type == T::INTEGER_LITERAL);

        auto punctuation = lex("a;b");
        CHECK("7. punto y coma junto a identificadores",
              types(punctuation) == std::vector<T>({T::IDENTIFIER, T::SEMICOLON,
                                                    T::IDENTIFIER, T::END_OF_FILE}));

        auto extensions = lex("SELECT * FROM t WHERE (a=1 OR b=2) AND c<>'x';");
        CHECK("7. extensiones asterisco y parentesis",
              extensions.ok() && extensions.tokens[1].type == T::STAR &&
              extensions.tokens[5].type == T::LPAREN);

        auto lines = lex("a\nb");
        CHECK("7. EOF conserva posicion final",
              lines.tokens.back().line == 2 && lines.tokens.back().column == 2);

        auto query = lex("WHERE valor >= -1e-10;");
        CHECK("7. numero cientifico signado dentro de consulta",
              types(query) == std::vector<T>({
                  T::KEYWORD_WHERE, T::IDENTIFIER, T::GREATER_EQUAL,
                  T::DECIMAL_LITERAL, T::SEMICOLON, T::END_OF_FILE
              }) && query.tokens[3].lexeme == "-1e-10" && query.ok());
    }

    std::cout << "\nResumen: " << passed << " OK, " << failed << " FAIL\n";
    return failed == 0 ? 0 : 1;
}
