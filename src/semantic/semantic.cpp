#include "semantic/semantic.hpp"

#include <unordered_set>

namespace sql {
namespace {

bool isNumeric(DataType t) { return t == DataType::Int || t == DataType::Float; }

// Simbolo legible de un operador para los mensajes y la impresion del AST.
const char* opSymbol(TokenType op) {
    switch (op) {
        case TokenType::PLUS:          return "+";
        case TokenType::MINUS:         return "-";
        case TokenType::STAR:          return "*";
        case TokenType::SLASH:         return "/";
        case TokenType::EQUAL:         return "=";
        case TokenType::NOT_EQUAL:     return "<>";
        case TokenType::LESS:          return "<";
        case TokenType::GREATER:       return ">";
        case TokenType::LESS_EQUAL:    return "<=";
        case TokenType::GREATER_EQUAL: return ">=";
        case TokenType::KEYWORD_AND:   return "AND";
        case TokenType::KEYWORD_OR:    return "OR";
        default:                       return "?";
    }
}

bool isArithmetic(TokenType op) {
    return op == TokenType::PLUS || op == TokenType::MINUS ||
           op == TokenType::STAR || op == TokenType::SLASH;
}
bool isEquality(TokenType op) {
    return op == TokenType::EQUAL || op == TokenType::NOT_EQUAL;
}
bool isOrdering(TokenType op) {
    return op == TokenType::LESS || op == TokenType::GREATER ||
           op == TokenType::LESS_EQUAL || op == TokenType::GREATER_EQUAL;
}

class Analyzer {
public:
    Analyzer(const Table& table, std::vector<SemDiag>& diags) : table_(table), diags_(diags) {}

    // Verifica tipos de un nodo del WHERE, lo decora con su tipo inferido y lo devuelve.
    // Unknown significa "ya se reporto un error aqui": no se encadenan mas errores.
    DataType check(const std::shared_ptr<AstNode>& node) {
        if (!node) return DataType::Unknown;
        DataType result = DataType::Unknown;
        switch (node->kind) {
            case AstNode::Kind::IntLit:    result = DataType::Int; break;
            case AstNode::Kind::FloatLit:  result = DataType::Float; break;
            case AstNode::Kind::StringLit: result = DataType::Varchar; break;
            case AstNode::Kind::Column: {
                const Column* col = table_.find(node->lexeme);
                if (!col) {
                    error(node, "la columna '" + node->lexeme + "' no pertenece a la tabla '" +
                                    table_.name + "'");
                } else {
                    result = col->type;
                }
                break;
            }
            case AstNode::Kind::Unary: {  // negacion unaria
                const DataType t = check(node->left);
                if (t == DataType::Unknown) break;
                if (!isNumeric(t)) {
                    error(node, "el operando de '-' unario debe ser numerico, es " +
                                    std::string(dataTypeName(t)));
                } else {
                    result = t;
                }
                break;
            }
            case AstNode::Kind::Binary:
                result = checkBinary(node);
                break;
        }
        node->type = result;
        return result;
    }

private:
    const Table& table_;
    std::vector<SemDiag>& diags_;

    void error(const std::shared_ptr<AstNode>& at, const std::string& msg) {
        diags_.push_back({msg, at->line, at->column, false});
    }

    DataType checkBinary(const std::shared_ptr<AstNode>& node) {
        const DataType l = check(node->left);
        const DataType r = check(node->right);
        const TokenType op = node->op;
        if (l == DataType::Unknown || r == DataType::Unknown) return DataType::Unknown;

        if (isArithmetic(op)) {
            if (isNumeric(l) && isNumeric(r)) {
                return (l == DataType::Float || r == DataType::Float) ? DataType::Float
                                                                      : DataType::Int;
            }
            error(node, std::string("los operandos de '") + opSymbol(op) +
                            "' deben ser numericos, son " + dataTypeName(l) + " y " +
                            dataTypeName(r));
            return DataType::Unknown;
        }
        if (isEquality(op)) {
            const bool ok = (isNumeric(l) && isNumeric(r)) || (l == r);  // num-num, o tipos iguales
            if (!ok) {
                error(node, std::string("no se puede comparar ") + dataTypeName(l) + " con " +
                                dataTypeName(r) + " usando '" + opSymbol(op) + "'");
                return DataType::Unknown;
            }
            return DataType::Boolean;
        }
        if (isOrdering(op)) {
            if (isNumeric(l) && isNumeric(r)) return DataType::Boolean;
            error(node, std::string("el orden '") + opSymbol(op) +
                            "' requiere operandos numericos, son " + dataTypeName(l) + " y " +
                            dataTypeName(r));
            return DataType::Unknown;
        }
        // Logicos AND / OR.
        if (l == DataType::Boolean && r == DataType::Boolean) return DataType::Boolean;
        error(node, std::string("los operandos de '") + opSymbol(op) +
                        "' deben ser BOOLEAN, son " + dataTypeName(l) + " y " + dataTypeName(r));
        return DataType::Unknown;
    }
};

void printNode(const std::shared_ptr<AstNode>& node, int indent, std::ostream& out) {
    if (!node) return;
    for (int i = 0; i < indent; ++i) out << "  ";
    switch (node->kind) {
        case AstNode::Kind::Column:    out << "Column " << node->lexeme; break;
        case AstNode::Kind::IntLit:    out << "IntLit " << node->lexeme; break;
        case AstNode::Kind::FloatLit:  out << "FloatLit " << node->lexeme; break;
        case AstNode::Kind::StringLit: out << "StringLit " << node->lexeme; break;
        case AstNode::Kind::Unary:     out << "Unary -"; break;
        case AstNode::Kind::Binary:    out << "Binary " << opSymbol(node->op); break;
    }
    out << " [" << dataTypeName(node->type) << "]\n";
    printNode(node->left, indent + 1, out);
    printNode(node->right, indent + 1, out);
}

}  // namespace

SemResult analyze(SelectStmt stmt, const Catalog& catalog) {
    SemResult result;

    // 1. Existencia de la tabla (FROM).
    const Table* table = catalog.find(stmt.table);
    if (!table) {
        result.diagnostics.push_back({"la tabla '" + stmt.table + "' no existe en el catalogo",
                                      stmt.table_line, stmt.table_column, false});
        result.stmt = std::move(stmt);
        result.ok = false;
        return result;
    }

    // 2. Proyeccion: expansion de `*` o resolucion de columnas + duplicados.
    if (stmt.select_all) {
        stmt.projection.clear();
        for (const Column& c : table->columns) {
            stmt.projection.push_back({c.name, stmt.table_line, stmt.table_column, c.type, true});
        }
    } else {
        std::unordered_set<std::string> seen;
        for (ProjItem& item : stmt.projection) {
            const Column* col = table->find(item.name);
            if (!col) {
                result.diagnostics.push_back({"la columna '" + item.name +
                                                  "' no pertenece a la tabla '" + table->name + "'",
                                              item.line, item.column, false});
                continue;
            }
            item.type = col->type;
            if (!seen.insert(item.name).second) {
                result.diagnostics.push_back({"columna '" + item.name +
                                                  "' repetida en la proyeccion",
                                              item.line, item.column, true});  // advertencia
            }
        }
    }

    // 3. WHERE: verificacion de tipos; la condicion completa debe ser BOOLEAN.
    if (stmt.where) {
        Analyzer analyzer(*table, result.diagnostics);
        const DataType t = analyzer.check(stmt.where);
        if (t != DataType::Boolean && t != DataType::Unknown) {
            result.diagnostics.push_back({"la condicion WHERE debe ser BOOLEAN, es " +
                                              std::string(dataTypeName(t)),
                                          stmt.where->line, stmt.where->column, false});
        }
    }

    bool has_error = false;
    for (const SemDiag& d : result.diagnostics) {
        if (!d.warning) has_error = true;
    }
    result.ok = !has_error;
    result.stmt = std::move(stmt);
    return result;
}

void printDecoratedAst(const SelectStmt& stmt, std::ostream& out) {
    out << "SelectStmt\n";
    out << "  proyeccion" << (stmt.select_all ? " (SELECT * expandido)" : "") << ":\n";
    for (const ProjItem& item : stmt.projection) {
        out << "    " << item.name << " [" << dataTypeName(item.type) << "]\n";
    }
    out << "  from: " << stmt.table << "\n";
    if (stmt.where) {
        out << "  where:\n";
        printNode(stmt.where, 2, out);
    } else {
        out << "  where: (ninguno)\n";
    }
}

}  // namespace sql
