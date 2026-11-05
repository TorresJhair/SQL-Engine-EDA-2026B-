#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <string>

#include "lexer.hpp"

int main(int argc, char* argv[]) {
    if (argc > 2) {
        std::cerr << "Uso: " << argv[0] << " [archivo.sql]\n";
        return 2;
    }

    std::string input;
    if (argc == 2) {
        std::ifstream file(argv[1]);
        if (!file) {
            std::cerr << "No se pudo abrir: " << argv[1] << '\n';
            return 2;
        }
        input.assign(std::istreambuf_iterator<char>(file),
                     std::istreambuf_iterator<char>());
    } else {
        input.assign(std::istreambuf_iterator<char>(std::cin),
                     std::istreambuf_iterator<char>());
    }

    sql::LexResult result = sql::Lexer(input).tokenize();

    std::cout << std::left << std::setw(5) << "LIN" << std::setw(5) << "COL"
              << std::setw(18) << "TIPO" << "LEXEMA\n";
    for (const auto& token : result.tokens) {
        std::cout << std::setw(5) << token.line << std::setw(5) << token.column
                  << std::setw(18) << sql::tokenTypeName(token.type)
                  << token.lexeme << '\n';
    }

    for (const auto& error : result.errors) {
        std::cerr << "[ERROR LEXICO] linea " << error.line
                  << ", columna " << error.column << ": " << error.message
                  << " -> \"" << error.lexeme << "\"\n";
    }

    return result.ok() ? 0 : 1;
}
