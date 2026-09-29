// tests/test_storage.cpp                       [P1]
// SlottedPage: capacidad (131/132 y 8/9), el +4 del directorio, la cadena de libres por
// indices de slot (cambio 43), el borrado que no compacta y el 27 -> 54 -> 27 (cambio 41).
//
// Los numeros de este archivo salen de la aritmetica del layout, no de lo que el codigo
// devuelva: si el codigo se rompe, el test dice 132 donde esperaba 131 y no "algo distinto".
#include "TestHarness.h"

#include "common/Types.h"
#include "storage/SlottedPage.h"
#include "storage/Tuple.h"
#include "storage/Value.h"

#include <cstdint>
#include <string>
#include <vector>

namespace {

// La tupla del benchmark: INT key + VARCHAR name de 16 B = 1 + 7 + 19 = 27 B, y 27 + 4
// del directorio = los 31 B por tupla de la formula (page_size - 8) / 31.
std::vector<uint8_t> tupla27(int32_t key) {
    Tuple t;
    t.append(Value(key));
    t.append(Value(std::string(16, 'a')));
    std::vector<uint8_t> b;
    t.serializeTo(b);
    return b;
}

// La tupla minima: un VARCHAR vacio. Mide 4 B y por eso toda region liberada tiene lugar
// para el puntero de 2 B de la cadena.
std::vector<uint8_t> tupla4() {
    Tuple t;
    t.append(Value(std::string("")));
    std::vector<uint8_t> b;
    t.serializeTo(b);
    return b;
}

std::vector<uint8_t> bytesRaw(size_t n, uint8_t fill = 0xAA) {
    return std::vector<uint8_t>(n, fill);
}

// Lectura cruda de la cabecera y del directorio, para comprobar el LAYOUT y no solo la
// API. Si el codigo devuelve bien los valores con el directorio mal puesto, estos tests lo
// detectan y los de la API no.
uint16_t rd16(const Page& p, size_t pos) {
    return static_cast<uint16_t>(static_cast<uint16_t>(p[pos]) |
                                 (static_cast<uint16_t>(p[pos + 1]) << 8));
}
uint16_t nSlotsOf(const Page& p)    { return rd16(p, 0); }
uint16_t dataStartOf(const Page& p) { return rd16(p, 2); }
uint16_t dataEndOf(const Page& p)   { return rd16(p, 4); }
uint16_t freeHeadOf(const Page& p)  { return rd16(p, 6); }
size_t   dirPos(SlotID s)           { return 8 + 4 * s; }

constexpr uint16_t kNoSlot = 0xFFFF;

}  // namespace

// --- layout ----------------------------------------------------------------
TEST(storage, init_deja_la_cabecera_en_ceros) {
    SlottedPage sp = SlottedPage::init(4096);
    const Page  p  = sp.toPage();

    CHECK_EQ(p.size(), size_t(4096));
    CHECK_EQ(nSlotsOf(p), uint16_t(0));      // ningun slot todavia
    CHECK_EQ(dataStartOf(p), uint16_t(8));   // dataStart = fin del directorio vacio
    CHECK_EQ(dataEndOf(p), uint16_t(4096));  // dataEnd = el final de la pagina
    CHECK_EQ(freeHeadOf(p), kNoSlot);        // cadena vacia
    CHECK_EQ(sp.slotCount(), size_t(0));
    CHECK_EQ(sp.freeSlotCount(), size_t(0));
}

TEST(storage, init_deja_todo_el_resto_en_ceros) {
    // Una pagina con basura seria indistinguible de una recien creada.
    const Page p = SlottedPage::init(4096).toPage();
    for (size_t i = 8; i < p.size(); ++i) CHECK_EQ(int(p[i]), 0);
}

TEST(storage, data_start_es_siempre_8_mas_4_por_slot) {
    SlottedPage sp = SlottedPage::init(4096);
    for (int i = 0; i < 20; ++i) {
        SlotID s;
        CHECK(sp.insert(tupla27(i + 1), s) == Status::Ok);
        CHECK_EQ(dataStartOf(sp.toPage()), uint16_t(8 + 4 * (i + 1)));
    }
}

TEST(storage, los_datos_crecen_hacia_atras_desde_el_final) {
    // Cada tupla de 27 B se pega al final, así que el offset baja de a 27.
    SlottedPage sp = SlottedPage::init(4096);
    SlotID      s;
    CHECK(sp.insert(tupla27(1), s) == Status::Ok);
    CHECK_EQ(rd16(sp.toPage(), dirPos(0)), uint16_t(4096 - 27));
    CHECK(sp.insert(tupla27(2), s) == Status::Ok);
    CHECK_EQ(rd16(sp.toPage(), dirPos(1)), uint16_t(4096 - 54));
}

TEST(storage, max_tuple_size_es_la_pagina_menos_12) {
    // 8 de cabecera + 4 de UNA entrada de directorio: la tupla más grande que puede
    // existir es la que ocupa la pagina sola.
    CHECK_EQ(SlottedPage::maxTupleSize(4096), size_t(4084));
    CHECK_EQ(SlottedPage::maxTupleSize(256), size_t(244));
    CHECK_EQ(SlottedPage::maxTupleSize(12), size_t(0));
}

TEST(storage, to_page_y_wrap_conservan_la_pagina) {
    SlottedPage sp = SlottedPage::init(4096);
    SlotID      s;
    CHECK(sp.insert(tupla27(1), s) == Status::Ok);
    CHECK(sp.insert(tupla4(), s) == Status::Ok);

    const Page      raw = sp.toPage();
    SlottedPage     rt  = SlottedPage::wrap(raw);
    CHECK(rt.toPage() == raw);   // CHECK, no CHECK_EQ: un vector no tiene operator<<
    CHECK_EQ(rt.slotCount(), size_t(2));

    std::vector<uint8_t> out;
    CHECK(rt.lookup(0, out) == Status::Ok);
    CHECK_EQ(out.size(), size_t(27));
}

// --- insert / lookup -------------------------------------------------------
TEST(storage, insert_devuelve_el_slot_0) {
    SlottedPage sp = SlottedPage::init(4096);
    SlotID      s = 0xFFFF;
    CHECK(sp.insert(tupla27(1), s) == Status::Ok);
    CHECK_EQ(s, SlotID(0));
    CHECK_EQ(sp.slotCount(), size_t(1));
}

TEST(storage, insert_devuelve_slots_seguidos) {
    // El slotID es monotono: el directorio se indexa por slot y no por offset, así que dos
    // inserts nunca pueden devolver el mismo slot.
    SlottedPage sp = SlottedPage::init(4096);
    for (int i = 0; i < 10; ++i) {
        SlotID s = 0xFFFF;
        CHECK(sp.insert(tupla27(i + 1), s) == Status::Ok);
        CHECK_EQ(s, SlotID(i));
    }
}

TEST(storage, lookup_devuelve_los_mismos_bytes) {
    SlottedPage                sp = SlottedPage::init(4096);
    SlotID                     s;
    const std::vector<uint8_t> t = tupla27(7);
    CHECK(sp.insert(t, s) == Status::Ok);

    std::vector<uint8_t> out;
    CHECK(sp.lookup(s, out) == Status::Ok);
    CHECK(out == t);
}

TEST(storage, round_trip_de_varias_tuplas) {
    SlottedPage sp = SlottedPage::init(4096);
    for (int i = 0; i < 50; ++i) {
        SlotID s;
        CHECK(sp.insert(tupla27(1000 + i), s) == Status::Ok);
    }
    for (int i = 0; i < 50; ++i) {
        std::vector<uint8_t> out;
        CHECK(sp.lookup(SlotID(i), out) == Status::Ok);

        Tuple t;
        CHECK(Tuple::deserialize(out.data(), out.size(), t) == Status::Ok);
        CHECK_EQ(t.fieldCount(), size_t(2));
        CHECK_EQ(t.at(0).asInt(), 1000 + i);
    }
}

TEST(storage, los_slots_se_leen_en_cualquier_orden) {
    // El directorio se indexa por slot: leer el 7 y después el 0 tiene que dar lo mismo
    // que leerlos en orden.
    SlottedPage sp = SlottedPage::init(4096);
    for (int i = 0; i < 8; ++i) {
        SlotID s;
        CHECK(sp.insert(tupla27(i + 1), s) == Status::Ok);
    }
    std::vector<uint8_t> a, b;
    CHECK(sp.lookup(7, a) == Status::Ok);
    CHECK(sp.lookup(0, b) == Status::Ok);
    CHECK(sp.lookup(3, a) == Status::Ok);
    CHECK(sp.lookup(0, b) == Status::Ok);
    CHECK(a == tupla27(4));
    CHECK(b == tupla27(1));
}

TEST(storage, insert_acepta_bytes_crudos) {
    // El contrato acepta bytes crudos, no solo tuplas: lo unico que exige es >= 2 B.
    SlottedPage sp = SlottedPage::init(4096);
    SlotID      s;
    const std::vector<uint8_t> crudo = bytesRaw(9, 0x5A);
    CHECK(sp.insert(crudo, s) == Status::Ok);

    std::vector<uint8_t> out;
    CHECK(sp.lookup(s, out) == Status::Ok);
    CHECK(out == crudo);
}

TEST(storage, una_tupla_imposible_da_tuple_too_large) {
    // PageFull es "no queda lugar AHORA"; TupleTooLarge es "no entra NUNCA". Con 4096 B el
    // máximo es 4084.
    SlottedPage sp = SlottedPage::init(4096);
    SlotID      s;
    CHECK(sp.insert(bytesRaw(4085), s) == Status::TupleTooLarge);
    CHECK_EQ(sp.slotCount(), size_t(0));
    CHECK(sp.insert(bytesRaw(4084), s) == Status::Ok);
}

// --- capacidad -------------------------------------------------------------
TEST(storage, caben_131_tuplas_de_27_en_4096) {
    // 8 + 131*31 = 4069 <= 4096.
    SlottedPage sp = SlottedPage::init(4096);
    for (int i = 0; i < 131; ++i) {
        SlotID s = 0xFFFF;
        CHECK(sp.insert(tupla27(i + 1), s) == Status::Ok);
        CHECK_EQ(s, SlotID(i));
    }
    CHECK_EQ(sp.slotCount(), size_t(131));
    CHECK_EQ(sp.freeSpace(), size_t(27));
}

TEST(storage, la_132_da_page_full) {
    // 8 + 132*31 = 4100 > 4096.
    SlottedPage sp = SlottedPage::init(4096);
    for (int i = 0; i < 131; ++i) {
        SlotID s;
        CHECK(sp.insert(tupla27(i + 1), s) == Status::Ok);
    }
    SlotID s = 0xFFFF;
    CHECK(sp.insert(tupla27(132), s) == Status::PageFull);
    CHECK_EQ(sp.slotCount(), size_t(131));
}

TEST(storage, free_space_dice_27_pero_la_132_no_entra) {
    // ESTE es el caso del +4 que casi nadie suma. freeSpace() da 27 y la tupla pide 27, así
    // que un `freeSpace() >= len` ingenuo la acepta. No entra: escribir al final también
    // cuesta la entrada de directorio nueva, que empuja dataStart 4 B arriba, y
    // 27 < 27 + 4.
    SlottedPage sp = SlottedPage::init(4096);
    for (int i = 0; i < 131; ++i) {
        SlotID s;
        CHECK(sp.insert(tupla27(i + 1), s) == Status::Ok);
    }
    CHECK_EQ(sp.freeSpace(), size_t(27));
    CHECK(sp.freeSpace() >= tupla27(0).size());   // el chequeo ingenuo pasaria

    SlotID s;
    CHECK(sp.insert(tupla27(132), s) == Status::PageFull);
    CHECK_EQ(dataStartOf(sp.toPage()), uint16_t(8 + 4 * 131));
    CHECK_EQ(dataEndOf(sp.toPage()), uint16_t(4096 - 131 * 27));
}

TEST(storage, caben_8_tuplas_de_27_en_256) {
    // 8 + 8*31 = 256 exacto: la pagina queda llena.
    SlottedPage sp = SlottedPage::init(256);
    for (int i = 0; i < 8; ++i) {
        SlotID s = 0xFFFF;
        CHECK(sp.insert(tupla27(i + 1), s) == Status::Ok);
    }
    CHECK_EQ(sp.slotCount(), size_t(8));
    CHECK_EQ(sp.freeSpace(), size_t(0));
    CHECK_EQ(dataStartOf(sp.toPage()), dataEndOf(sp.toPage()));
}

TEST(storage, la_9_da_page_full_en_256) {
    SlottedPage sp = SlottedPage::init(256);
    for (int i = 0; i < 8; ++i) {
        SlotID s;
        CHECK(sp.insert(tupla27(i + 1), s) == Status::Ok);
    }
    SlotID s = 0xFFFF;
    CHECK(sp.insert(tupla27(9), s) == Status::PageFull);
    CHECK_EQ(sp.slotCount(), size_t(8));
}

TEST(storage, la_pagina_llena_de_4096_acepta_la_tupla_minima) {
    // Con la página llena de 27 B quedan 27 B, que no alcanzan para otra de 27 pero sí
    // para una de 4... salvo por el +4. 27 >= 4 + 4, así que ESTA sí entra.
    SlottedPage sp = SlottedPage::init(4096);
    for (int i = 0; i < 131; ++i) {
        SlotID s;
        CHECK(sp.insert(tupla27(i + 1), s) == Status::Ok);
    }
    SlotID s = 0xFFFF;
    CHECK(sp.insert(tupla4(), s) == Status::Ok);
    CHECK_EQ(s, SlotID(131));
    // Los 27 libres se vuelven 19: 4 B de la tupla nueva y 4 B de su entrada de directorio.
    CHECK_EQ(sp.freeSpace(), size_t(27 - 4 - 4));
}

TEST(storage, la_tupla_minima_entra_de_a_uno) {
    // 4 B de datos + 4 de directorio = 8 B por tupla minima: (4096 - 8) / 8 = 511.
    SlottedPage sp = SlottedPage::init(4096);
    for (int i = 0; i < 511; ++i) {
        SlotID s = 0xFFFF;
        CHECK(sp.insert(tupla4(), s) == Status::Ok);
    }
    CHECK_EQ(sp.slotCount(), size_t(511));
    CHECK_EQ(sp.freeSpace(), size_t(0));
    SlotID s;
    CHECK(sp.insert(tupla4(), s) == Status::PageFull);
}

TEST(storage, la_pagina_mas_chica_que_sostiene_una_tupla_minima) {
    // 8 de cabecera + 4 de un slot + 2 de la región más pequeña que el contrato acepta = 14.
    // Con 14 entra UNA tupla de 2 B y no hay lugar para una segunda, que necesitaría 18.
    // Con 12 no cabe ni una, así que init() lo rechaza con un assert: es una página que no
    // puede contener ninguna tupla, no una página vacía.
    SlottedPage sp = SlottedPage::init(14);
    SlotID      s = 0xFFFF;
    CHECK(sp.insert(bytesRaw(2, 0x66), s) == Status::Ok);
    CHECK_EQ(s, SlotID(0));
    CHECK_EQ(sp.freeSpace(), size_t(0));
    CHECK(sp.insert(bytesRaw(2, 0x66), s) == Status::PageFull);
}

// --- erase no compacta -----------------------------------------------------
TEST(storage, erase_no_mueve_los_datos_de_los_demas) {
    // El motivo de que erase no compacte: si moviera bytes, los offsets de los slots
    // siguientes cambiarían. Se compara la pagina byte a byte ANTES y DESPUES, salvo los 2
    // bytes de la region liberada (que ahora guardan el puntero de la cadena) y los 2 de
    // la cabecera (freeHead).
    SlottedPage sp = SlottedPage::init(4096);
    for (int i = 0; i < 5; ++i) {
        SlotID s;
        CHECK(sp.insert(tupla27(i + 1), s) == Status::Ok);
    }
    const Page antes = sp.toPage();

    CHECK(sp.erase(2) == Status::Ok);
    const Page despues = sp.toPage();

    const size_t off2 = rd16(antes, dirPos(2));
    for (size_t i = 0; i < antes.size(); ++i) {
        if (i >= 6 && i < 8) continue;         // freeHead
        if (i >= off2 && i < off2 + 2) continue;  // el puntero de la cadena
        CHECK_EQ(int(despues[i]), int(antes[i]));
    }
}

TEST(storage, erase_no_cambia_el_offset_de_los_demas) {
    SlottedPage sp = SlottedPage::init(4096);
    for (int i = 0; i < 5; ++i) {
        SlotID s;
        CHECK(sp.insert(tupla27(i + 1), s) == Status::Ok);
    }
    const uint16_t off0 = rd16(sp.toPage(), dirPos(0));
    const uint16_t off1 = rd16(sp.toPage(), dirPos(1));
    const uint16_t off3 = rd16(sp.toPage(), dirPos(3));
    const uint16_t off4 = rd16(sp.toPage(), dirPos(4));

    CHECK(sp.erase(2) == Status::Ok);

    CHECK_EQ(rd16(sp.toPage(), dirPos(0)), off0);
    CHECK_EQ(rd16(sp.toPage(), dirPos(1)), off1);
    CHECK_EQ(rd16(sp.toPage(), dirPos(3)), off3);
    CHECK_EQ(rd16(sp.toPage(), dirPos(4)), off4);
}

TEST(storage, erase_no_baja_data_end_ni_el_numero_de_slots) {
    SlottedPage sp = SlottedPage::init(4096);
    for (int i = 0; i < 5; ++i) {
        SlotID s;
        CHECK(sp.insert(tupla27(i + 1), s) == Status::Ok);
    }
    const uint16_t data_end_antes = dataEndOf(sp.toPage());

    CHECK(sp.erase(1) == Status::Ok);

    CHECK_EQ(dataEndOf(sp.toPage()), data_end_antes);
    CHECK_EQ(nSlotsOf(sp.toPage()), uint16_t(5));   // el slot sigue, ahora libre
}

TEST(storage, el_insert_despues_de_borrar_no_baja_data_end) {
    // El reuse del slot liberado no gasta espacio del final: dataEnd se queda donde estaba.
    SlottedPage sp = SlottedPage::init(4096);
    for (int i = 0; i < 5; ++i) {
        SlotID s;
        CHECK(sp.insert(tupla27(i + 1), s) == Status::Ok);
    }
    const uint16_t data_end_antes = dataEndOf(sp.toPage());

    CHECK(sp.erase(1) == Status::Ok);
    SlotID s = 0xFFFF;
    CHECK(sp.insert(tupla27(99), s) == Status::Ok);

    CHECK_EQ(s, SlotID(1));                        // reusó el slot, no escribió al final
    CHECK_EQ(dataEndOf(sp.toPage()), data_end_antes);
}

TEST(storage, free_space_crece_al_borrar) {
    SlottedPage sp = SlottedPage::init(4096);
    for (int i = 0; i < 5; ++i) {
        SlotID s;
        CHECK(sp.insert(tupla27(i + 1), s) == Status::Ok);
    }
    const size_t antes = sp.freeSpace();
    CHECK(sp.erase(3) == Status::Ok);
    CHECK_EQ(sp.freeSpace(), antes + 27);
    CHECK(sp.erase(0) == Status::Ok);
    CHECK_EQ(sp.freeSpace(), antes + 54);
}

TEST(storage, lookup_de_un_slot_borrado_da_not_found) {
    SlottedPage sp = SlottedPage::init(4096);
    SlotID      s;
    CHECK(sp.insert(tupla27(1), s) == Status::Ok);
    CHECK(sp.insert(tupla27(2), s) == Status::Ok);
    CHECK(sp.erase(0) == Status::Ok);

    std::vector<uint8_t> out;
    CHECK(sp.lookup(0, out) == Status::NotFound);
    CHECK(sp.lookup(1, out) == Status::Ok);
}

TEST(storage, erase_de_un_slot_ya_libre_da_not_found) {
    SlottedPage sp = SlottedPage::init(4096);
    SlotID      s;
    CHECK(sp.insert(tupla27(1), s) == Status::Ok);
    CHECK(sp.erase(0) == Status::Ok);
    CHECK(sp.erase(0) == Status::NotFound);
    CHECK_EQ(sp.freeSlotCount(), size_t(1));   // no se enlazó dos veces
}

TEST(storage, erase_y_lookup_de_un_slot_fuera_de_rango_dan_not_found) {
    SlottedPage sp = SlottedPage::init(4096);
    SlotID      s;
    CHECK(sp.insert(tupla27(1), s) == Status::Ok);

    std::vector<uint8_t> out;
    CHECK(sp.lookup(1, out) == Status::NotFound);
    CHECK(sp.lookup(4095, out) == Status::NotFound);
    CHECK(sp.erase(1) == Status::NotFound);
    CHECK(sp.erase(4095) == Status::NotFound);
}

// --- cambio 43: la cadena por indices de slot ------------------------------
TEST(storage, la_cadena_usa_indices_de_slot_no_offsets) {
    // freeHead y el puntero de la region son los dos slotID. 3 tuplas de 27 B: la region
    // del slot 1 arranca en 4096 - 54 = 4042.
    SlottedPage sp = SlottedPage::init(4096);
    for (int i = 0; i < 3; ++i) {
        SlotID s;
        CHECK(sp.insert(tupla27(i + 1), s) == Status::Ok);
    }
    CHECK(sp.erase(1) == Status::Ok);
    const Page p = sp.toPage();

    CHECK_EQ(freeHeadOf(p), uint16_t(1));              // un slotID, no un offset
    CHECK_EQ(rd16(p, 4042), kNoSlot);                  // y el puntero de la region, 0xFFFF
    CHECK_EQ(sp.freeSlotCount(), size_t(1));
}

TEST(storage, la_cadena_encadena_en_el_orden_de_los_borrados) {
    // Se borra 1 y después 2: la cabeza es 2 y el 2 apunta a 1. Es una pila LIFO, y el
    // first-fit la recorre de cabeza a cola.
    SlottedPage sp = SlottedPage::init(4096);
    for (int i = 0; i < 3; ++i) {
        SlotID s;
        CHECK(sp.insert(tupla27(i + 1), s) == Status::Ok);
    }
    CHECK(sp.erase(1) == Status::Ok);
    CHECK(sp.erase(2) == Status::Ok);
    const Page p = sp.toPage();

    // Cada insert pega al final de la pagina, asi que el slot 0 queda en 4096-27, el 1 en
    // 4096-54 y el 2 en 4096-81: el ULTIMO insertado es el de offset MENOR.
    CHECK_EQ(freeHeadOf(p), uint16_t(2));
    CHECK_EQ(rd16(p, 4096 - 81), uint16_t(1));   // la region del 2 apunta al slot 1
    CHECK_EQ(rd16(p, 4096 - 54), kNoSlot);      // el 1 cierra la cadena
    CHECK_EQ(sp.freeSlotCount(), size_t(2));
}

TEST(storage, al_liberar_dos_y_reusar_el_primero_el_segundo_sigue_en_la_cadena) {
    // El caso que pide el checklist del cambio 43. Se borran dos slots, se reusa UNO, y el
    // otro tiene que seguir en la cadena: su length se sigue sumando en freeSpace y se
    // puede reusar después.
    SlottedPage sp = SlottedPage::init(4096);
    for (int i = 0; i < 5; ++i) {
        SlotID s;
        CHECK(sp.insert(tupla27(i + 1), s) == Status::Ok);
    }
    CHECK(sp.erase(1) == Status::Ok);
    CHECK(sp.erase(3) == Status::Ok);

    const size_t libre_2 = sp.freeSpace();
    CHECK_EQ(sp.freeSlotCount(), size_t(2));

    // Reusa el primero de la cadena (el 3, que es la cabeza) con una tupla de 5 B.
    SlotID reusado = 0xFFFF;
    CHECK(sp.insert(bytesRaw(5, 0x11), reusado) == Status::Ok);
    CHECK_EQ(reusado, SlotID(3));

    // El 1 SIGUE libre: sigue en la cadena y su length sigue sumando.
    CHECK_EQ(sp.freeSlotCount(), size_t(1));
    CHECK_EQ(freeHeadOf(sp.toPage()), uint16_t(1));
    CHECK_EQ(sp.freeSpace(), libre_2 - 27);   // bajó el length del 3, el del 1 sigue

    // Y se puede reusar después.
    SlotID segundo = 0xFFFF;
    CHECK(sp.insert(bytesRaw(5, 0x22), segundo) == Status::Ok);
    CHECK_EQ(segundo, SlotID(1));
    CHECK_EQ(sp.freeSlotCount(), size_t(0));
    CHECK_EQ(freeHeadOf(sp.toPage()), kNoSlot);
}

TEST(storage, el_length_del_slot_libre_no_se_toca_al_desenlazar) {
    // Al reusar, el length del directorio no baja: por eso la tupla menor deja cola.
    SlottedPage sp = SlottedPage::init(4096);
    SlotID      s;
    CHECK(sp.insert(tupla27(1), s) == Status::Ok);
    CHECK(sp.erase(0) == Status::Ok);

    SlotID r = 0xFFFF;
    CHECK(sp.insert(tupla4(), r) == Status::Ok);
    CHECK_EQ(r, SlotID(0));
    CHECK_EQ(rd16(sp.toPage(), dirPos(0) + 2), uint16_t(27));   // sigue siendo 27
}

TEST(storage, una_pagina_llena_deja_la_cadena_vacia) {
    // El checklist pide comprobar freeHead == 0xFFFF con la pagina llena.
    SlottedPage sp = SlottedPage::init(4096);
    for (int i = 0; i < 131; ++i) {
        SlotID s;
        CHECK(sp.insert(tupla27(i + 1), s) == Status::Ok);
    }
    CHECK_EQ(sp.freeSlotCount(), size_t(0));
    CHECK_EQ(freeHeadOf(sp.toPage()), kNoSlot);
}

TEST(storage, el_first_fit_toma_el_primero_que_le_sirve) {
    // Dos slots libres de tamaños distintos. Se reusa el primero de la cadena, aunque el
    // segundo sea más grande: first-fit, no best-fit.
    SlottedPage sp = SlottedPage::init(4096);
    SlotID      s;
    CHECK(sp.insert(tupla4(), s) == Status::Ok);     // slot 0, 4 B
    CHECK(sp.insert(tupla27(1), s) == Status::Ok);   // slot 1, 27 B
    CHECK(sp.insert(tupla4(), s) == Status::Ok);     // slot 2, 4 B

    CHECK(sp.erase(0) == Status::Ok);                // 4 B
    CHECK(sp.erase(1) == Status::Ok);                // 27 B, ahora cabeza
    CHECK_EQ(freeHeadOf(sp.toPage()), uint16_t(1));

    SlotID r = 0xFFFF;
    CHECK(sp.insert(bytesRaw(20, 0x33), r) == Status::Ok);
    CHECK_EQ(r, SlotID(1));      // el 4 B del slot 0 no le alcanzaba
    CHECK_EQ(freeHeadOf(sp.toPage()), uint16_t(0));   // el 0 quedó como cabeza
    CHECK_EQ(sp.freeSlotCount(), size_t(1));
}

TEST(storage, un_slot_libre_pequeno_no_se_parte) {
    // first-fit es "la primera región que le sirva ENTERA": un slot de 4 B no puede
    // alojar una tupla de 20 B partiéndose, así que se lo saltea.
    SlottedPage sp = SlottedPage::init(4096);
    SlotID      s;
    CHECK(sp.insert(tupla4(), s) == Status::Ok);
    CHECK(sp.erase(0) == Status::Ok);

    SlotID r = 0xFFFF;
    CHECK(sp.insert(bytesRaw(20, 0x44), r) == Status::Ok);
    CHECK_EQ(r, SlotID(1));      // escribió al final, no reusó el de 4 B
    CHECK_EQ(sp.freeSlotCount(), size_t(1));
    CHECK_EQ(freeHeadOf(sp.toPage()), uint16_t(0));
}

// --- cambio 41: reutilizar con sobrante -------------------------------------
TEST(storage, el_27_54_27) {
    // ESTE es el caso numerico del checklist. Página de 4096 llena con 131 tuplas de 27:
    // freeSpace da 27. Se borra una: 27 + 27 = 54. Se inserta una tupla de 4, que reusa la
    // región: freeSpace baja los 27 del length COMPLETO, no los 4 de la tupla, y vuelve a
    // dar 27.
    SlottedPage sp = SlottedPage::init(4096);
    for (int i = 0; i < 131; ++i) {
        SlotID s;
        CHECK(sp.insert(tupla27(i + 1), s) == Status::Ok);
    }
    CHECK_EQ(sp.freeSpace(), size_t(27));

    CHECK(sp.erase(130) == Status::Ok);
    CHECK_EQ(sp.freeSpace(), size_t(54));

    SlotID r = 0xFFFF;
    CHECK(sp.insert(tupla4(), r) == Status::Ok);
    CHECK_EQ(r, SlotID(130));
    CHECK_EQ(sp.freeSpace(), size_t(27));
}

TEST(storage, lookup_devuelve_la_region_entera_con_cola) {
    // La región liberada era de 27 y la tupla nueva de 4: lookup devuelve 27, no 4.
    SlottedPage sp = SlottedPage::init(4096);
    SlotID      s;
    CHECK(sp.insert(tupla27(1), s) == Status::Ok);
    CHECK(sp.erase(0) == Status::Ok);
    SlotID r = 0xFFFF;
    CHECK(sp.insert(tupla4(), r) == Status::Ok);

    // Los 4 primeros B son la tupla NUEVA (pisan la region) y los 23 de cola son el resto
    // de la tupla vieja: no se puede comparar contra tupla27(1) entera, porque el inicio ya
    // no es el de antes.
    std::vector<uint8_t> out;
    CHECK(sp.lookup(r, out) == Status::Ok);
    CHECK_EQ(out.size(), size_t(27));
    const std::vector<uint8_t> nueva = tupla4();
    const std::vector<uint8_t> vieja = tupla27(1);
    for (size_t i = 0; i < nueva.size(); ++i) CHECK_EQ(int(out[i]), int(nueva[i]));
    for (size_t i = nueva.size(); i < 27; ++i) CHECK_EQ(int(out[i]), int(vieja[i]));
}

TEST(storage, deserialize_ignora_la_cola) {
    // El otro extremo del cambio 41: la cola no puede dar Corrupt, porque son datos
    // válidos de la tupla anterior.
    SlottedPage sp = SlottedPage::init(4096);
    SlotID      s;
    CHECK(sp.insert(tupla27(1), s) == Status::Ok);
    CHECK(sp.erase(0) == Status::Ok);
    SlotID r = 0xFFFF;
    CHECK(sp.insert(tupla4(), r) == Status::Ok);

    std::vector<uint8_t> out;
    CHECK(sp.lookup(r, out) == Status::Ok);
    CHECK_EQ(out.size(), size_t(27));

    Tuple t;
    CHECK(Tuple::deserialize(out.data(), out.size(), t) == Status::Ok);
    CHECK_EQ(t.fieldCount(), size_t(1));
    CHECK_EQ(t.at(0).asVarChar(), std::string(""));
    CHECK_EQ(t.serializedSize(), size_t(4));
}

TEST(storage, un_segundo_borrado_recupera_la_region_entera) {
    // Después del reuso de 4 B, el length del directorio sigue siendo 27. Un segundo
    // erase + insert de 27 B tiene que recuperar la región entera.
    SlottedPage sp = SlottedPage::init(4096);
    for (int i = 0; i < 131; ++i) {
        SlotID s;
        CHECK(sp.insert(tupla27(i + 1), s) == Status::Ok);
    }
    CHECK(sp.erase(130) == Status::Ok);
    SlotID r = 0xFFFF;
    CHECK(sp.insert(tupla4(), r) == Status::Ok);
    CHECK_EQ(sp.freeSpace(), size_t(27));

    CHECK(sp.erase(130) == Status::Ok);
    CHECK_EQ(sp.freeSpace(), size_t(54));

    SlotID r2 = 0xFFFF;
    CHECK(sp.insert(tupla27(500), r2) == Status::Ok);
    CHECK_EQ(r2, SlotID(130));
    CHECK_EQ(sp.freeSpace(), size_t(27));

    std::vector<uint8_t> out;
    CHECK(sp.lookup(130, out) == Status::Ok);
    CHECK_EQ(out.size(), size_t(27));
    Tuple t;
    CHECK(Tuple::deserialize(out.data(), out.size(), t) == Status::Ok);
    CHECK_EQ(t.at(0).asInt(), 500);
}

TEST(storage, borrar_todo_y_volver_a_llenar) {
    // Borra los 131, queda una sola región gigante en la cadena (131*27 + 27) y se vuelven
    // a insertar 131 tuplas. Si la cadena o el length estuvieran mal, algo se rompería acá.
    SlottedPage sp = SlottedPage::init(4096);
    for (int i = 0; i < 131; ++i) {
        SlotID s;
        CHECK(sp.insert(tupla27(i + 1), s) == Status::Ok);
    }
    for (int i = 0; i < 131; ++i) CHECK(sp.erase(SlotID(i)) == Status::Ok);

    CHECK_EQ(sp.freeSlotCount(), size_t(131));
    CHECK_EQ(freeHeadOf(sp.toPage()), SlotID(130));
    CHECK_EQ(sp.freeSpace(), size_t(131 * 27 + 27));

    for (int i = 0; i < 131; ++i) {
        SlotID s = 0xFFFF;
        CHECK(sp.insert(tupla27(1000 + i), s) == Status::Ok);
        CHECK_EQ(s, SlotID(130 - i));   // LIFO: la cabeza es el último borrado
    }
    CHECK_EQ(sp.freeSlotCount(), size_t(0));
    CHECK_EQ(sp.freeSpace(), size_t(27));

    // La primera tupla insertada es la 1000 y cae en la cabeza de la cadena, que es el
    // último slot borrado (el 130); la última insertada es la 1130 y cae en el slot 0.
    // O sea: el slot j queda con la tupla 1000 + (130 - j).
    for (int i = 0; i < 131; ++i) {
        std::vector<uint8_t> out;
        CHECK(sp.lookup(SlotID(i), out) == Status::Ok);
        Tuple t;
        CHECK(Tuple::deserialize(out.data(), out.size(), t) == Status::Ok);
        CHECK_EQ(t.at(0).asInt(), 1000 + (130 - i));
    }
}

// --- el puntero de 2 B de la cadena ----------------------------------------
TEST(storage, la_tupla_minima_de_4_b_deja_lugar_para_el_puntero) {
    // La tupla más pequeña posible mide 4 B, así que toda región liberada tiene sitio
    // para su puntero de 2 B. No existe el caso length < 2, y por eso el puntero puede
    // vivir dentro de la región y no aparte.
    SlottedPage sp = SlottedPage::init(4096);
    for (int i = 0; i < 20; ++i) {
        SlotID s = 0xFFFF;
        CHECK(sp.insert(tupla4(), s) == Status::Ok);
    }
    for (int i = 0; i < 20; ++i) CHECK(sp.erase(SlotID(i)) == Status::Ok);

    // La cadena queda 19 -> 18 -> ... -> 0 -> 0xFFFF. Recorrerla entera y terminar en
    // kNoSlot es lo que prueba que el puntero CABE en una región de 4 B: si no cupiera, el
    // puntero se habría comido bytes de la tupla y la cadena perdería el camino de vuelta.
    const Page p = sp.toPage();
    CHECK_EQ(freeHeadOf(p), uint16_t(19));
    SlotID s    = freeHeadOf(p);
    int    hops = 0;
    while (s != kNoSlot && hops <= 20) {
        CHECK_EQ(s, SlotID(19 - hops));
        s = rd16(p, rd16(p, dirPos(s)));   // el puntero vive en la region, no en el directorio
        ++hops;
    }
    CHECK_EQ(s, kNoSlot);
    CHECK_EQ(hops, 20);
    CHECK_EQ(sp.freeSlotCount(), size_t(20));
}

TEST(storage, la_region_mas_pequena_que_se_puede_liberar_mide_2) {
    // El contrato acepta bytes crudos de 2 B, y esa es la región liberada más pequeña
    // posible: 2 B, justo los del puntero. Si el puntero no entrara, la cadena se rompería
    // al liberar. El slot 0 queda en 4094 y el 1 en 4092, porque cada insert pega al final.
    SlottedPage sp = SlottedPage::init(4096);
    SlotID      s;
    CHECK(sp.insert(bytesRaw(2, 0x77), s) == Status::Ok);
    CHECK(sp.insert(bytesRaw(2, 0x77), s) == Status::Ok);
    CHECK_EQ(rd16(sp.toPage(), dirPos(0)), uint16_t(4094));
    CHECK_EQ(rd16(sp.toPage(), dirPos(1)), uint16_t(4092));

    CHECK(sp.erase(0) == Status::Ok);
    CHECK_EQ(rd16(sp.toPage(), 4094), kNoSlot);   // el 0 era el único libre
    CHECK_EQ(freeHeadOf(sp.toPage()), uint16_t(0));

    CHECK(sp.erase(1) == Status::Ok);
    CHECK_EQ(freeHeadOf(sp.toPage()), uint16_t(1));
    CHECK_EQ(rd16(sp.toPage(), 4092), uint16_t(0));   // el 1 apunta al 0
    CHECK_EQ(sp.freeSlotCount(), size_t(2));
}

// --- muchos ciclos de borrar y reusar ---------------------------------------
TEST(storage, cien_ciclos_de_borrar_y_reusar) {
    // Estrés de la cadena: si un puntero se pierde o un length se corrompe, la cuenta de
    // freeSpace deja de cuadrar en algún momento.
    SlottedPage                sp = SlottedPage::init(4096);
    std::vector<SlotID>        slots;
    for (int i = 0; i < 100; ++i) {
        SlotID s = 0xFFFF;
        CHECK(sp.insert(tupla27(i + 1), s) == Status::Ok);
        slots.push_back(s);
    }
    const size_t lleno = sp.freeSpace();

    for (int i = 0; i < 100; i += 2) {
        CHECK(sp.erase(slots[i]) == Status::Ok);
        CHECK(sp.erase(slots[i + 1]) == Status::Ok);
    }
    CHECK_EQ(sp.freeSlotCount(), size_t(100));
    CHECK_EQ(sp.freeSpace(), lleno + 100 * 27);

    for (int i = 0; i < 100; ++i) {
        SlotID s = 0xFFFF;
        CHECK(sp.insert(tupla27(2000 + i), s) == Status::Ok);
    }
    CHECK_EQ(sp.freeSlotCount(), size_t(0));
    CHECK_EQ(freeHeadOf(sp.toPage()), kNoSlot);
    CHECK_EQ(sp.freeSpace(), lleno);

    for (int i = 0; i < 100; ++i) {
        std::vector<uint8_t> out;
        CHECK(sp.lookup(slots[i], out) == Status::Ok);
        Tuple t;
        CHECK(Tuple::deserialize(out.data(), out.size(), t) == Status::Ok);
        CHECK_EQ(t.at(0).asInt(), 2000 + (99 - (i / 2) * 2 - (i % 2)));
    }
}

namespace suites {
void storage() {
    // Los TEST() de este archivo se registran solos; ver TestHarness.h.
}
}  // namespace suites
