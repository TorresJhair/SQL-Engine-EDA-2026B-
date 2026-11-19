#include "semantic/catalog.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace sql {

const char* dataTypeName(DataType t) {
    switch (t) {
        case DataType::Int:     return "INT";
        case DataType::Float:   return "FLOAT";
        case DataType::Varchar: return "VARCHAR";
        case DataType::Boolean: return "BOOLEAN";
        case DataType::Unknown: return "UNKNOWN";
    }
    return "UNKNOWN";
}

DataType dataTypeFromName(const std::string& name) {
    std::string upper;
    upper.reserve(name.size());
    for (char c : name) upper += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    if (upper == "INT") return DataType::Int;
    if (upper == "FLOAT") return DataType::Float;
    if (upper == "VARCHAR") return DataType::Varchar;
    if (upper == "BOOLEAN") return DataType::Boolean;
    return DataType::Unknown;
}

const Column* Table::find(const std::string& column) const {
    auto it = by_name.find(column);
    return it == by_name.end() ? nullptr : &columns[static_cast<size_t>(it->second)];
}

void Catalog::addColumn(const std::string& table, const std::string& column, DataType type,
                        bool pk) {
    Table& t = tables_[table];
    if (t.name.empty()) t.name = table;
    if (t.by_name.count(column)) return;  // columna duplicada: se conserva la primera
    const int ordinal = static_cast<int>(t.columns.size());
    t.by_name[column] = ordinal;
    t.columns.push_back(Column{column, ordinal, type, pk});
}

const Table* Catalog::find(const std::string& table) const {
    auto it = tables_.find(table);
    return it == tables_.end() ? nullptr : &it->second;
}

namespace {
std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}
}  // namespace

Catalog loadCsv(const std::string& path) {
    std::ifstream file(path);
    if (!file) throw std::runtime_error("No se pudo abrir el catalogo: " + path);

    Catalog catalog;
    std::string line;
    int lineno = 0;
    while (std::getline(file, line)) {
        ++lineno;
        const std::string row = trim(line);
        if (row.empty() || row[0] == '#') continue;

        std::stringstream ss(row);
        std::string table, column, type, pk;
        std::getline(ss, table, ',');
        std::getline(ss, column, ',');
        std::getline(ss, type, ',');
        std::getline(ss, pk, ',');
        table = trim(table);
        column = trim(column);
        type = trim(type);
        pk = trim(pk);
        if (table.empty() || column.empty() || type.empty()) {
            throw std::runtime_error("Fila invalida en el catalogo (linea " +
                                     std::to_string(lineno) + "): " + row);
        }
        const DataType dt = dataTypeFromName(type);
        if (dt == DataType::Unknown) {
            throw std::runtime_error("Tipo desconocido '" + type + "' en el catalogo (linea " +
                                     std::to_string(lineno) + ")");
        }
        const bool is_pk = (pk == "1" || pk == "true" || pk == "TRUE" || pk == "pk");
        catalog.addColumn(table, column, dt, is_pk);
    }
    return catalog;
}

Catalog defaultSchema() {
    Catalog c;
    c.addColumn("estudiantes", "id", DataType::Int, true);
    c.addColumn("estudiantes", "nombre", DataType::Varchar);
    c.addColumn("estudiantes", "edad", DataType::Int);
    c.addColumn("estudiantes", "promedio", DataType::Float);
    c.addColumn("estudiantes", "activo", DataType::Boolean);

    c.addColumn("cursos", "id", DataType::Int, true);
    c.addColumn("cursos", "titulo", DataType::Varchar);
    c.addColumn("cursos", "creditos", DataType::Int);

    c.addColumn("matriculas", "id", DataType::Int, true);
    c.addColumn("matriculas", "estudiante_id", DataType::Int);
    c.addColumn("matriculas", "curso_id", DataType::Int);
    c.addColumn("matriculas", "nota", DataType::Float);
    return c;
}

}  // namespace sql
