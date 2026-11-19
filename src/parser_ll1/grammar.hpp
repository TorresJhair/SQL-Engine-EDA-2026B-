#pragma once

#include "lexer/token.hpp"

#include <map>
#include <ostream>
#include <set>
#include <string>
#include <vector>

namespace sql {

// No terminales de la gramatica LL(1). El simbolo inicial es Consulta.
enum class NonTerminal {
    Consulta, Proyeccion, ListaCols, ColsTail, WhereOpt,
    Condicion, Disy, DisyTail, Conj, ConjTail, Comp, CompTail,
    OpComp, Suma, SumaTail, Prod, ProdTail, Unario, Primario
};

// Un simbolo de la gramatica es terminal (un TokenType) o no terminal.
struct Symbol {
    enum class Kind { Terminal, NonTerminal };
    Kind kind = Kind::Terminal;
    TokenType terminal = TokenType::END_OF_FILE;
    NonTerminal nonterminal = NonTerminal::Consulta;

    static Symbol term(TokenType t) { return {Kind::Terminal, t, NonTerminal::Consulta}; }
    static Symbol nonterm(NonTerminal n) { return {Kind::NonTerminal, TokenType::END_OF_FILE, n}; }
    bool isTerminal() const { return kind == Kind::Terminal; }
};

// Una produccion A -> X1 X2 ... Xn. Un rhs vacio representa A -> epsilon.
struct Production {
    NonTerminal lhs;
    std::vector<Symbol> rhs;
};

// Gramatica LL(1): produce las producciones, calcula FIRST/FOLLOW y arma la
// tabla de parseo M[A, a]. El constructor hace todo el trabajo; si la gramatica
// tuviera conflictos LL(1) quedarian listados en conflicts().
class Grammar {
public:
    Grammar();

    const std::vector<Production>& productions() const { return productions_; }
    const Production& production(int id) const { return productions_[static_cast<size_t>(id)]; }

    // Celda M[A, a]: id de produccion a aplicar, o -1 si la celda esta vacia.
    int tableEntry(NonTerminal A, TokenType a) const;

    const std::set<TokenType>& first(NonTerminal A) const { return first_.at(A); }
    const std::set<TokenType>& follow(NonTerminal A) const { return follow_.at(A); }
    bool nullable(NonTerminal A) const { return nullable_.at(A); }

    // Terminales con entrada en la fila de A: el conjunto que se "esperaba"
    // cuando la celda M[A, token_actual] esta vacia.
    std::set<TokenType> expected(NonTerminal A) const;

    // Conflictos LL(1) hallados al construir la tabla. Vacio => gramatica LL(1).
    const std::vector<std::string>& conflicts() const { return conflicts_; }

    void dumpProductions(std::ostream& out) const;
    void dumpFirstFollow(std::ostream& out) const;
    void dumpTable(std::ostream& out) const;

    static NonTerminal startSymbol() { return NonTerminal::Consulta; }
    static const char* name(NonTerminal A);
    static std::string symbolName(const Symbol& s);
    static std::string productionString(const Production& p);
    static const std::vector<TokenType>& allTerminals();
    static const std::vector<NonTerminal>& allNonTerminals();

private:
    std::vector<Production> productions_;
    std::map<NonTerminal, std::set<TokenType>> first_;
    std::map<NonTerminal, std::set<TokenType>> follow_;
    std::map<NonTerminal, bool> nullable_;
    std::map<NonTerminal, std::map<TokenType, int>> table_;
    std::vector<std::string> conflicts_;

    void computeNullable();
    void computeFirst();
    void computeFollow();
    void buildTable();

    // Agrega a out los FIRST de la secuencia seq[from..]; devuelve true si toda
    // esa secuencia es anulable (deriva epsilon).
    bool firstOfSeq(const std::vector<Symbol>& seq, size_t from, std::set<TokenType>& out) const;
};

}  // namespace sql
