#include "parser/grammar.hpp"

#include <algorithm>
#include <iomanip>

namespace sql {
namespace {

using NT = NonTerminal;
using T = TokenType;

Symbol t(TokenType type) { return Symbol::term(type); }
Symbol n(NonTerminal nt) { return Symbol::nonterm(nt); }

}  // namespace

Grammar::Grammar() {
    // Gramatica LL(1) estricta (sin recursion izquierda y factorizada). La
    // numeracion de los comentarios coincide con src/parser/grammar.md.
    productions_ = {
        // 1
        {NT::Consulta, {t(T::KEYWORD_SELECT), n(NT::Proyeccion), t(T::KEYWORD_FROM),
                        t(T::IDENTIFIER), n(NT::WhereOpt), t(T::SEMICOLON)}},
        // 2, 3
        {NT::Proyeccion, {t(T::STAR)}},
        {NT::Proyeccion, {n(NT::ListaCols)}},
        // 4
        {NT::ListaCols, {t(T::IDENTIFIER), n(NT::ColsTail)}},
        // 5, 6
        {NT::ColsTail, {t(T::COMMA), t(T::IDENTIFIER), n(NT::ColsTail)}},
        {NT::ColsTail, {}},
        // 7, 8
        {NT::WhereOpt, {t(T::KEYWORD_WHERE), n(NT::Condicion)}},
        {NT::WhereOpt, {}},
        // 9
        {NT::Condicion, {n(NT::Disy)}},
        // 10
        {NT::Disy, {n(NT::Conj), n(NT::DisyTail)}},
        // 11, 12
        {NT::DisyTail, {t(T::KEYWORD_OR), n(NT::Conj), n(NT::DisyTail)}},
        {NT::DisyTail, {}},
        // 13
        {NT::Conj, {n(NT::Comp), n(NT::ConjTail)}},
        // 14, 15
        {NT::ConjTail, {t(T::KEYWORD_AND), n(NT::Comp), n(NT::ConjTail)}},
        {NT::ConjTail, {}},
        // 16
        {NT::Comp, {n(NT::Suma), n(NT::CompTail)}},
        // 17, 18
        {NT::CompTail, {n(NT::OpComp), n(NT::Suma)}},
        {NT::CompTail, {}},
        // 19-24
        {NT::OpComp, {t(T::EQUAL)}},
        {NT::OpComp, {t(T::NOT_EQUAL)}},
        {NT::OpComp, {t(T::LESS)}},
        {NT::OpComp, {t(T::GREATER)}},
        {NT::OpComp, {t(T::LESS_EQUAL)}},
        {NT::OpComp, {t(T::GREATER_EQUAL)}},
        // 25
        {NT::Suma, {n(NT::Prod), n(NT::SumaTail)}},
        // 26, 27, 28
        {NT::SumaTail, {t(T::PLUS), n(NT::Prod), n(NT::SumaTail)}},
        {NT::SumaTail, {t(T::MINUS), n(NT::Prod), n(NT::SumaTail)}},
        {NT::SumaTail, {}},
        // 29
        {NT::Prod, {n(NT::Unario), n(NT::ProdTail)}},
        // 30, 31, 32
        {NT::ProdTail, {t(T::STAR), n(NT::Unario), n(NT::ProdTail)}},
        {NT::ProdTail, {t(T::SLASH), n(NT::Unario), n(NT::ProdTail)}},
        {NT::ProdTail, {}},
        // 33, 34
        {NT::Unario, {t(T::MINUS), n(NT::Unario)}},
        {NT::Unario, {n(NT::Primario)}},
        // 35-39
        {NT::Primario, {t(T::IDENTIFIER)}},
        {NT::Primario, {t(T::INTEGER_LITERAL)}},
        {NT::Primario, {t(T::DECIMAL_LITERAL)}},
        {NT::Primario, {t(T::STRING_LITERAL)}},
        {NT::Primario, {t(T::LPAREN), n(NT::Condicion), t(T::RPAREN)}},
    };

    for (NonTerminal nt : allNonTerminals()) {
        first_[nt] = {};
        follow_[nt] = {};
        nullable_[nt] = false;
    }

    computeNullable();
    computeFirst();
    computeFollow();
    buildTable();
}

const std::vector<NonTerminal>& Grammar::allNonTerminals() {
    static const std::vector<NonTerminal> kAll = {
        NT::Consulta, NT::Proyeccion, NT::ListaCols, NT::ColsTail, NT::WhereOpt,
        NT::Condicion, NT::Disy, NT::DisyTail, NT::Conj, NT::ConjTail, NT::Comp,
        NT::CompTail, NT::OpComp, NT::Suma, NT::SumaTail, NT::Prod, NT::ProdTail,
        NT::Unario, NT::Primario};
    return kAll;
}

const std::vector<TokenType>& Grammar::allTerminals() {
    static const std::vector<TokenType> kAll = {
        T::KEYWORD_SELECT, T::KEYWORD_FROM, T::KEYWORD_WHERE, T::KEYWORD_AND, T::KEYWORD_OR,
        T::IDENTIFIER, T::INTEGER_LITERAL, T::DECIMAL_LITERAL, T::STRING_LITERAL,
        T::EQUAL, T::NOT_EQUAL, T::LESS, T::GREATER, T::LESS_EQUAL, T::GREATER_EQUAL,
        T::PLUS, T::MINUS, T::SLASH, T::COMMA, T::SEMICOLON, T::STAR, T::LPAREN, T::RPAREN,
        T::END_OF_FILE};
    return kAll;
}

void Grammar::computeNullable() {
    bool changed = true;
    while (changed) {
        changed = false;
        for (const Production& p : productions_) {
            if (nullable_[p.lhs]) continue;
            bool all_nullable = true;
            for (const Symbol& s : p.rhs) {
                if (s.isTerminal() || !nullable_[s.nonterminal]) {
                    all_nullable = false;
                    break;
                }
            }
            if (all_nullable) {  // rhs vacio tambien cae aqui (A -> epsilon)
                nullable_[p.lhs] = true;
                changed = true;
            }
        }
    }
}

void Grammar::computeFirst() {
    bool changed = true;
    while (changed) {
        changed = false;
        for (const Production& p : productions_) {
            std::set<TokenType>& dst = first_[p.lhs];
            const size_t before = dst.size();
            for (const Symbol& s : p.rhs) {
                if (s.isTerminal()) {
                    dst.insert(s.terminal);
                    break;
                }
                const std::set<TokenType>& src = first_[s.nonterminal];
                dst.insert(src.begin(), src.end());
                if (!nullable_[s.nonterminal]) break;
            }
            if (dst.size() != before) changed = true;
        }
    }
}

bool Grammar::firstOfSeq(const std::vector<Symbol>& seq, size_t from,
                         std::set<TokenType>& out) const {
    for (size_t i = from; i < seq.size(); ++i) {
        const Symbol& s = seq[i];
        if (s.isTerminal()) {
            out.insert(s.terminal);
            return false;
        }
        const std::set<TokenType>& src = first_.at(s.nonterminal);
        out.insert(src.begin(), src.end());
        if (!nullable_.at(s.nonterminal)) return false;
    }
    return true;  // toda la secuencia es anulable
}

void Grammar::computeFollow() {
    follow_[startSymbol()].insert(T::END_OF_FILE);  // $ sigue al simbolo inicial
    bool changed = true;
    while (changed) {
        changed = false;
        for (const Production& p : productions_) {
            for (size_t i = 0; i < p.rhs.size(); ++i) {
                if (p.rhs[i].isTerminal()) continue;
                NonTerminal B = p.rhs[i].nonterminal;
                std::set<TokenType>& dst = follow_[B];
                const size_t before = dst.size();

                std::set<TokenType> tail;
                const bool tail_nullable = firstOfSeq(p.rhs, i + 1, tail);
                dst.insert(tail.begin(), tail.end());
                if (tail_nullable) {
                    const std::set<TokenType>& fa = follow_[p.lhs];
                    dst.insert(fa.begin(), fa.end());
                }
                if (dst.size() != before) changed = true;
            }
        }
    }
}

void Grammar::buildTable() {
    for (size_t id = 0; id < productions_.size(); ++id) {
        const Production& p = productions_[id];
        std::set<TokenType> firstAlpha;
        const bool nullAlpha = firstOfSeq(p.rhs, 0, firstAlpha);

        std::set<TokenType> cells = firstAlpha;
        if (nullAlpha) {
            const std::set<TokenType>& fa = follow_[p.lhs];
            cells.insert(fa.begin(), fa.end());
        }

        for (TokenType a : cells) {
            auto& row = table_[p.lhs];
            auto it = row.find(a);
            if (it != row.end() && it->second != static_cast<int>(id)) {
                conflicts_.push_back(std::string("conflicto en M[") + name(p.lhs) + ", " +
                                     tokenTypeName(a) + "]: producciones " +
                                     std::to_string(it->second + 1) + " y " +
                                     std::to_string(id + 1));
            } else {
                row[a] = static_cast<int>(id);
            }
        }
    }
}

int Grammar::tableEntry(NonTerminal A, TokenType a) const {
    auto row = table_.find(A);
    if (row == table_.end()) return -1;
    auto cell = row->second.find(a);
    return cell == row->second.end() ? -1 : cell->second;
}

std::set<TokenType> Grammar::expected(NonTerminal A) const {
    std::set<TokenType> result;
    auto row = table_.find(A);
    if (row != table_.end()) {
        for (const auto& entry : row->second) result.insert(entry.first);
    }
    return result;
}

const char* Grammar::name(NonTerminal A) {
    switch (A) {
        case NT::Consulta:   return "consulta";
        case NT::Proyeccion: return "proyeccion";
        case NT::ListaCols:  return "lista_cols";
        case NT::ColsTail:   return "cols_tail";
        case NT::WhereOpt:   return "where_opt";
        case NT::Condicion:  return "condicion";
        case NT::Disy:       return "disy";
        case NT::DisyTail:   return "disy_tail";
        case NT::Conj:       return "conj";
        case NT::ConjTail:   return "conj_tail";
        case NT::Comp:       return "comp";
        case NT::CompTail:   return "comp_tail";
        case NT::OpComp:     return "op_comp";
        case NT::Suma:       return "suma";
        case NT::SumaTail:   return "suma_tail";
        case NT::Prod:       return "prod";
        case NT::ProdTail:   return "prod_tail";
        case NT::Unario:     return "unario";
        case NT::Primario:   return "primario";
    }
    return "?";
}

std::string Grammar::symbolName(const Symbol& s) {
    if (s.isTerminal()) {
        return s.terminal == T::END_OF_FILE ? "$" : tokenTypeName(s.terminal);
    }
    return name(s.nonterminal);
}

std::string Grammar::productionString(const Production& p) {
    std::string out = name(p.lhs);
    out += " -> ";
    if (p.rhs.empty()) {
        out += "epsilon";
        return out;
    }
    for (size_t i = 0; i < p.rhs.size(); ++i) {
        if (i) out += ' ';
        out += symbolName(p.rhs[i]);
    }
    return out;
}

void Grammar::dumpProductions(std::ostream& out) const {
    out << "Producciones (" << productions_.size() << "):\n";
    for (size_t i = 0; i < productions_.size(); ++i) {
        out << "  (" << std::setw(2) << (i + 1) << ") " << productionString(productions_[i]) << '\n';
    }
}

void Grammar::dumpFirstFollow(std::ostream& out) const {
    out << "FIRST / FOLLOW por no terminal:\n";
    for (NonTerminal nt : allNonTerminals()) {
        out << "  " << std::left << std::setw(11) << name(nt) << " FIRST = { ";
        for (TokenType a : first_.at(nt)) out << tokenTypeName(a) << ' ';
        if (nullable_.at(nt)) out << "epsilon ";
        out << "}\n" << std::setw(13) << " " << " FOLLOW = { ";
        for (TokenType a : follow_.at(nt)) out << (a == T::END_OF_FILE ? "$" : tokenTypeName(a)) << ' ';
        out << "}\n";
    }
}

void Grammar::dumpTable(std::ostream& out) const {
    if (conflicts_.empty()) {
        out << "Tabla LL(1) sin conflictos. Celdas M[A, a] = numero de produccion:\n";
    } else {
        out << "Tabla LL(1) CON CONFLICTOS:\n";
        for (const std::string& c : conflicts_) out << "  " << c << '\n';
    }
    for (NonTerminal nt : allNonTerminals()) {
        auto row = table_.find(nt);
        if (row == table_.end()) continue;
        for (const auto& entry : row->second) {
            out << "  M[" << std::left << std::setw(11) << name(nt) << ", "
                << std::setw(15) << tokenTypeName(entry.first) << "] = ("
                << (entry.second + 1) << ") " << productionString(productions_[static_cast<size_t>(entry.second)])
                << '\n';
        }
    }
}

}  // namespace sql
