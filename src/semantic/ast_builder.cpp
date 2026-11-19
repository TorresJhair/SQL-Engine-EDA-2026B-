#include "semantic/ast_builder.hpp"

#include <cassert>

namespace sql {
namespace {

// Recorre la derivacion (producciones aplicadas) y la lista de tokens emparejados, en
// paralelo, reconstruyendo el AST. Cada metodo build* corresponde a un no terminal y
// devuelve su atributo sintetizado (el subarbol). Las colas reciben el subarbol
// izquierdo como atributo heredado y asocian a la izquierda.
class Builder {
public:
    Builder(const ParseResult& result, const Grammar& grammar)
        : prod_(result.derivation), toks_(result.matched), grammar_(grammar) {}

    SelectStmt buildStmt() {
        nextProd(NonTerminal::Consulta);  // consulta -> SELECT proyeccion FROM IDENT where_opt ;
        SelectStmt stmt;
        eat();  // SELECT
        buildProyeccion(stmt);
        eat();  // FROM
        const Token table = eat();  // IDENTIFIER
        stmt.table = table.lexeme;
        stmt.table_line = table.line;
        stmt.table_column = table.column;
        buildWhereOpt(stmt);
        eat();  // SEMICOLON
        return stmt;
    }

private:
    const std::vector<int>& prod_;
    const std::vector<Token>& toks_;
    const Grammar& grammar_;
    size_t pi_ = 0;
    size_t ti_ = 0;

    const Production& nextProd(NonTerminal expected) {
        assert(pi_ < prod_.size());
        const Production& p = grammar_.production(prod_[pi_++]);
        assert(p.lhs == expected);
        (void)expected;
        return p;
    }
    const Token& eat() {
        assert(ti_ < toks_.size());
        return toks_[ti_++];
    }
    // ¿La produccion elegida es epsilon (rhs vacio)?
    static bool isEpsilon(const Production& p) { return p.rhs.empty(); }
    // Primer simbolo del rhs (para distinguir alternativas).
    static const Symbol& head(const Production& p) { return p.rhs.front(); }

    static std::shared_ptr<AstNode> binary(TokenType op, const Token& at,
                                           std::shared_ptr<AstNode> l,
                                           std::shared_ptr<AstNode> r) {
        auto n = std::make_shared<AstNode>();
        n->kind = AstNode::Kind::Binary;
        n->op = op;
        n->line = at.line;
        n->column = at.column;
        n->left = std::move(l);
        n->right = std::move(r);
        return n;
    }
    static std::shared_ptr<AstNode> leaf(AstNode::Kind kind, const Token& tok) {
        auto n = std::make_shared<AstNode>();
        n->kind = kind;
        n->lexeme = tok.lexeme;
        n->line = tok.line;
        n->column = tok.column;
        return n;
    }

    void buildProyeccion(SelectStmt& stmt) {
        const Production& p = nextProd(NonTerminal::Proyeccion);
        if (head(p).isTerminal()) {  // proyeccion -> STAR
            eat();                   // STAR
            stmt.select_all = true;
        } else {                     // proyeccion -> lista_cols
            buildListaCols(stmt);
        }
    }
    void buildListaCols(SelectStmt& stmt) {
        nextProd(NonTerminal::ListaCols);  // IDENTIFIER cols_tail
        const Token id = eat();
        stmt.projection.push_back({id.lexeme, id.line, id.column, DataType::Unknown, false});
        buildColsTail(stmt);
    }
    void buildColsTail(SelectStmt& stmt) {
        const Production& p = nextProd(NonTerminal::ColsTail);
        if (isEpsilon(p)) return;
        eat();  // COMMA
        const Token id = eat();
        stmt.projection.push_back({id.lexeme, id.line, id.column, DataType::Unknown, false});
        buildColsTail(stmt);
    }
    void buildWhereOpt(SelectStmt& stmt) {
        const Production& p = nextProd(NonTerminal::WhereOpt);
        if (isEpsilon(p)) return;
        eat();  // WHERE
        stmt.where = buildCondicion();
    }

    std::shared_ptr<AstNode> buildCondicion() {
        nextProd(NonTerminal::Condicion);  // condicion -> disy
        return buildDisy();
    }
    std::shared_ptr<AstNode> buildDisy() {
        nextProd(NonTerminal::Disy);  // conj disy_tail
        return buildDisyTail(buildConj());
    }
    std::shared_ptr<AstNode> buildDisyTail(std::shared_ptr<AstNode> left) {
        const Production& p = nextProd(NonTerminal::DisyTail);
        if (isEpsilon(p)) return left;
        const Token op = eat();  // OR
        auto node = binary(TokenType::KEYWORD_OR, op, std::move(left), buildConj());
        return buildDisyTail(node);
    }
    std::shared_ptr<AstNode> buildConj() {
        nextProd(NonTerminal::Conj);  // comp conj_tail
        return buildConjTail(buildComp());
    }
    std::shared_ptr<AstNode> buildConjTail(std::shared_ptr<AstNode> left) {
        const Production& p = nextProd(NonTerminal::ConjTail);
        if (isEpsilon(p)) return left;
        const Token op = eat();  // AND
        auto node = binary(TokenType::KEYWORD_AND, op, std::move(left), buildComp());
        return buildConjTail(node);
    }
    std::shared_ptr<AstNode> buildComp() {
        nextProd(NonTerminal::Comp);  // suma comp_tail
        return buildCompTail(buildSuma());
    }
    std::shared_ptr<AstNode> buildCompTail(std::shared_ptr<AstNode> left) {
        const Production& p = nextProd(NonTerminal::CompTail);
        if (isEpsilon(p)) return left;  // comp_tail -> op_comp suma (una sola comparacion)
        const Token op = buildOpComp();
        return binary(op.type, op, std::move(left), buildSuma());
    }
    Token buildOpComp() {
        nextProd(NonTerminal::OpComp);  // op_comp -> EQUAL | NOT_EQUAL | ...
        return eat();                   // el operador relacional emparejado
    }
    std::shared_ptr<AstNode> buildSuma() {
        nextProd(NonTerminal::Suma);  // prod suma_tail
        return buildSumaTail(buildProd());
    }
    std::shared_ptr<AstNode> buildSumaTail(std::shared_ptr<AstNode> left) {
        const Production& p = nextProd(NonTerminal::SumaTail);
        if (isEpsilon(p)) return left;
        const Token op = eat();  // PLUS o MINUS
        auto node = binary(op.type, op, std::move(left), buildProd());
        return buildSumaTail(node);
    }
    std::shared_ptr<AstNode> buildProd() {
        nextProd(NonTerminal::Prod);  // unario prod_tail
        return buildProdTail(buildUnario());
    }
    std::shared_ptr<AstNode> buildProdTail(std::shared_ptr<AstNode> left) {
        const Production& p = nextProd(NonTerminal::ProdTail);
        if (isEpsilon(p)) return left;
        const Token op = eat();  // STAR o SLASH
        auto node = binary(op.type, op, std::move(left), buildUnario());
        return buildProdTail(node);
    }
    std::shared_ptr<AstNode> buildUnario() {
        const Production& p = nextProd(NonTerminal::Unario);
        if (head(p).isTerminal()) {  // unario -> MINUS unario
            const Token op = eat();  // MINUS
            auto n = std::make_shared<AstNode>();
            n->kind = AstNode::Kind::Unary;
            n->op = TokenType::MINUS;
            n->line = op.line;
            n->column = op.column;
            n->left = buildUnario();
            return n;
        }
        return buildPrimario();  // unario -> primario
    }
    std::shared_ptr<AstNode> buildPrimario() {
        const Production& p = nextProd(NonTerminal::Primario);
        const Symbol& h = head(p);  // siempre terminal: IDENTIFIER, literal o LPAREN
        if (h.terminal == TokenType::LPAREN) {
            // primario -> LPAREN condicion RPAREN  (los parentesis no crean nodo)
            eat();  // LPAREN
            auto inner = buildCondicion();
            eat();  // RPAREN
            return inner;
        }
        const Token tok = eat();
        switch (h.terminal) {
            case TokenType::IDENTIFIER:      return leaf(AstNode::Kind::Column, tok);
            case TokenType::INTEGER_LITERAL: return leaf(AstNode::Kind::IntLit, tok);
            case TokenType::DECIMAL_LITERAL: return leaf(AstNode::Kind::FloatLit, tok);
            case TokenType::STRING_LITERAL:  return leaf(AstNode::Kind::StringLit, tok);
            default:                         return leaf(AstNode::Kind::Column, tok);  // inalcanzable
        }
    }
};

}  // namespace

SelectStmt buildAst(const ParseResult& result, const Grammar& grammar) {
    Builder builder(result, grammar);
    return builder.buildStmt();
}

}  // namespace sql
