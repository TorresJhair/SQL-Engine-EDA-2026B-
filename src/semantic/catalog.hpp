#pragma once

#include <string>
#include <unordered_map>
#include <vector>

namespace sql {

// Tipos de dato que soporta el catalogo (Lab 6).
enum class DataType { Int, Float, Varchar, Boolean, Unknown };

const char* dataTypeName(DataType t);
// Convierte "INT"/"FLOAT"/"VARCHAR"/"BOOLEAN" (sin importar mayusculas) a DataType.
DataType dataTypeFromName(const std::string& name);

struct Column {
    std::string name;
    int ordinal = 0;              // posicion ordinal dentro de la tabla (0-based)
    DataType type = DataType::Unknown;
    bool pk = false;
};

struct Table {
    std::string name;
    std::vector<Column> columns;                   // en orden ordinal
    std::unordered_map<std::string, int> by_name;  // nombre -> indice en columns

    const Column* find(const std::string& column) const;
};

// Tabla de Simbolos / Catalogo del sistema: metadatos de tablas y columnas.
// Consulta O(1) promedio por tabla (unordered_map) y por columna dentro de la tabla.
class Catalog {
public:
    // Agrega una columna a una tabla (la crea si no existe). El ordinal es el orden
    // de insercion. Ignora duplicados de columna (se queda con la primera).
    void addColumn(const std::string& table, const std::string& column, DataType type,
                   bool pk = false);

    const Table* find(const std::string& table) const;
    size_t size() const { return tables_.size(); }

private:
    std::unordered_map<std::string, Table> tables_;
};

// Carga un catalogo desde un CSV con filas `tabla,columna,tipo[,pk]`. Lineas vacias y
// las que empiezan con '#' se ignoran; `pk` es 1/true si la columna es llave primaria.
// Lanza std::runtime_error si el archivo no abre o una fila esta mal formada.
Catalog loadCsv(const std::string& path);

// Esquema de prueba en memoria (3 tablas con tipos variados), usado por los tests y por
// el CLI cuando no se pasa --schema.
Catalog defaultSchema();

}  // namespace sql
