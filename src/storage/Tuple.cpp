// src/storage/Tuple.cpp                        [P1]
#include "storage/Tuple.h"

#include <cassert>

// Los accessors triviales van en el .cpp y no en el header porque el header solo DECLARA
// las firmas: si las definiciones quedaran solo declaradas, el link de los tests falla con
// "undefined reference to Tuple::append".
void Tuple::append(const Value& v) { fields_.push_back(v); }

const Value& Tuple::at(size_t i) const {
    // Assert de error de programacion (indice fuera de rango dentro de una funcion ya
    // llamada con el tipo equivocado), no una precondicion testeada: ver §7.
    assert(i < fields_.size());
    return fields_[i];
}

size_t Tuple::fieldCount() const { return fields_.size(); }

size_t Tuple::serializedSize() const {
    // 1 B de nFields + los campos. serializeTo impide que una tupla vacia llegue a disco,
    // pero aca el conteo da 1 y no es un error: es el tamano que teria si se serializara.
    size_t n = 1;
    for (const Value& v : fields_) n += v.serializedSize();
    return n;
}

Status Tuple::serializeTo(std::vector<uint8_t>& out) const {
    // Precondicion de la regla de la tupla minima: una tupla sin campos mediria 1 B, y el
    // puntero de 2 B de la cadena de libres no entraria en la region liberada. Se devuelve
    // Status y NO se asserta, porque es una de las tres precondiciones que un test tiene que
    // poder comprobar sin fork y sin depender de -DNDEBUG (cambio 40, regla 21 de §7).
    //
    // `out` no se toca: si hay un error, el buffer queda como estaba, para que quien llama
    // no pueda usar medio contenido.
    if (fields_.empty()) return Status::PreconditionFailed;

    out.push_back(static_cast<uint8_t>(fields_.size()));  // [nFields: u8]
    for (const Value& v : fields_) v.serializeTo(out);
    return Status::Ok;
}

Status Tuple::deserialize(const uint8_t* data, size_t len, Tuple& out) {
    if (len < 1) return Status::Corrupt;

    const uint8_t* p = data;
    const uint8_t* end = data + len;

    const uint8_t n_fields = *p;
    ++p;

    Tuple t;
    for (uint8_t i = 0; i < n_fields; ++i) {
        // Value no tiene constructor por defecto, asi que se inicializa con un valor
        // cualquiera: deserialize lo sobreescribe SIEMPRE que devuelva Ok, y si devuelve
        // Corrupt se sale del ciclo sin usarlo.
        Value v(0);
        // Value::deserialize devuelve Corrupt si un len DECLARADO se pasa de `end`, que es
        // el unico error que puede aparecer aca: el puntero ya esta acotado por `len`.
        if (Value::deserialize(p, end, v) != Status::Ok) return Status::Corrupt;
        t.append(v);
    }

    // Lo que sobra del buffer se IGNORA, sin error. No es una decision arbitraria: al
    // reutilizar una region liberada mas grande que la tupla nueva, `len` es el tamano de
    // la REGION, no el de la tupla, y los bytes de mas son de la tupla vieja. Si esto
    // devolviera Corrupt, reutilizar una region de 27 B con una tupla de 4 B daria error
    // sobre datos validos (cambio 41).
    out = t;
    return Status::Ok;
}
