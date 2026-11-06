#include "parser/parser.hpp"

#include <algorithm>

namespace sql {
namespace {

// Describe el token observado para los mensajes de error.
std::string describe(const Token& tok) {
    if (tok.type == TokenType::END_OF_FILE) return "fin de entrada";
    return std::string("'") + tok.lexeme + "' (" + tokenTypeName(tok.type) + ")";
}

// Une los nombres de un conjunto de terminales esperados.
std::string joinExpected(const std::set<TokenType>& expected) {
    std::string out;
    for (TokenType a : expected) {
        if (!out.empty()) out += ", ";
        out += (a == TokenType::END_OF_FILE) ? "$" : tokenTypeName(a);
    }
    return out;
}

}  // namespace

Parser::Parser(const LexResult& input) : input_(input) {
    // El motor asume un END_OF_FILE al final (lo usa como marcador $).
    if (input_.tokens.empty() || input_.tokens.back().type != TokenType::END_OF_FILE) {
        const Token end = input_.tokens.empty()
            ? Token{TokenType::END_OF_FILE, "", 1, 1}
            : Token{TokenType::END_OF_FILE, "", input_.tokens.back().line,
                    input_.tokens.back().column +
                        static_cast<int>(input_.tokens.back().lexeme.size())};
        input_.tokens.push_back(end);
    }
}

ParseResult Parser::parse() {
    ParseResult result;

    // Un error lexico impide el analisis sintactico: se propaga como fallo.
    if (!input_.errors.empty()) {
        for (const auto& e : input_.errors) {
            result.errors.push_back({e.message, e.lexeme, e.line, e.column, true});
        }
        return result;
    }

    // Pila con el tope en back(). Fondo: $ (END_OF_FILE); encima el simbolo inicial.
    std::vector<Symbol> stack;
    stack.push_back(Symbol::term(TokenType::END_OF_FILE));
    stack.push_back(Symbol::nonterm(Grammar::startSymbol()));

    size_t pos = 0;
    const auto current = [&]() -> const Token& {
        return input_.tokens[std::min(pos, input_.tokens.size() - 1)];
    };

    // Render del estado actual para la traza.
    const auto renderStack = [&]() {
        std::string out;
        for (const Symbol& s : stack) {  // fondo -> tope (tope a la derecha)
            if (!out.empty()) out += ' ';
            out += Grammar::symbolName(s);
        }
        return out;
    };
    const auto renderInput = [&]() {
        std::string out;
        for (size_t i = pos; i < input_.tokens.size(); ++i) {
            if (!out.empty()) out += ' ';
            const Token& tk = input_.tokens[i];
            out += (tk.type == TokenType::END_OF_FILE) ? "$" : tk.lexeme;
        }
        return out;
    };

    while (!stack.empty()) {
        const Symbol top = stack.back();
        const Token& tok = current();

        if (top.isTerminal()) {
            if (top.terminal == tok.type) {
                result.trace.push_back({renderStack(), renderInput(),
                                        "emparejar " + Grammar::symbolName(top)});
                stack.pop_back();
                ++pos;
            } else {
                const std::string expected =
                    top.terminal == TokenType::END_OF_FILE ? "$" : tokenTypeName(top.terminal);
                result.errors.push_back(
                    {"se esperaba " + expected + ", se encontro " + describe(tok),
                     tok.lexeme, tok.line, tok.column, false});
                break;
            }
        } else {
            const int id = grammar_.tableEntry(top.nonterminal, tok.type);
            if (id < 0) {
                result.errors.push_back(
                    {"se esperaba uno de {" + joinExpected(grammar_.expected(top.nonterminal)) +
                         "}, se encontro " + describe(tok),
                     tok.lexeme, tok.line, tok.column, false});
                break;
            }
            const Production& prod = grammar_.production(id);
            result.trace.push_back({renderStack(), renderInput(),
                                    "expandir (" + std::to_string(id + 1) + ") " +
                                        Grammar::productionString(prod)});
            stack.pop_back();
            for (auto it = prod.rhs.rbegin(); it != prod.rhs.rend(); ++it) {
                stack.push_back(*it);  // empuja el rhs en orden inverso
            }
        }
    }

    result.accepted = result.errors.empty() && stack.empty();
    return result;
}

}  // namespace sql
