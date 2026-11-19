#include "semantic/catalog.hpp"

#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>

namespace {
int passed = 0;
int failed = 0;

#define CHECK(name, condition) do { \
    if (condition) { ++passed; std::cout << "[OK]   " << name << '\n'; } \
    else { ++failed; std::cout << "[FAIL] " << name << " (linea " << __LINE__ << ")\n"; } \
} while (false)
}  // namespace

int main() {
    using sql::DataType;

    // Esquema en memoria.
    const sql::Catalog c = sql::defaultSchema();
    CHECK("3 tablas en el esquema por defecto", c.size() == 3);

    const sql::Table* est = c.find("estudiantes");
    CHECK("tabla estudiantes existe", est != nullptr);
    CHECK("tabla inexistente da nullptr", c.find("profesores") == nullptr);

    if (est) {
        CHECK("estudiantes tiene 5 columnas", est->columns.size() == 5);
        const sql::Column* edad = est->find("edad");
        CHECK("columna edad es INT", edad && edad->type == DataType::Int);
        const sql::Column* promedio = est->find("promedio");
        CHECK("columna promedio es FLOAT", promedio && promedio->type == DataType::Float);
        const sql::Column* activo = est->find("activo");
        CHECK("columna activo es BOOLEAN", activo && activo->type == DataType::Boolean);
        CHECK("id es PK en ordinal 0", est->columns[0].name == "id" &&
              est->columns[0].pk && est->columns[0].ordinal == 0);
        CHECK("orden ordinal preservado (nombre en 1)", est->columns[1].name == "nombre");
        CHECK("columna inexistente da nullptr", est->find("telefono") == nullptr);
    }

    // Carga desde CSV temporal.
    const std::string path = "catalog_test_tmp.csv";
    {
        std::ofstream f(path);
        f << "# comentario\n\n";
        f << "ventas,id,INT,pk\n";
        f << "ventas,monto,FLOAT\n";
        f << "ventas,cliente,VARCHAR\n";
    }
    const sql::Catalog csv = sql::loadCsv(path);
    const sql::Table* ventas = csv.find("ventas");
    CHECK("CSV: tabla ventas cargada", ventas != nullptr);
    if (ventas) {
        CHECK("CSV: 3 columnas", ventas->columns.size() == 3);
        CHECK("CSV: monto es FLOAT", ventas->find("monto") &&
              ventas->find("monto")->type == DataType::Float);
        CHECK("CSV: id es PK", ventas->find("id") && ventas->find("id")->pk);
    }
    std::remove(path.c_str());

    // Tipo desde nombre (case-insensitive).
    CHECK("dataTypeFromName varchar", sql::dataTypeFromName("varchar") == DataType::Varchar);
    CHECK("dataTypeFromName desconocido", sql::dataTypeFromName("blob") == DataType::Unknown);

    std::cout << "Resumen: " << passed << " OK, " << failed << " FAIL\n";
    return failed == 0 ? 0 : 1;
}
