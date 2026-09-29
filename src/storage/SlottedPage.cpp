// src/storage/SlottedPage.cpp                  [P1]  (puro, sin I/O)
//
// Layout (todo u16, little-endian, el mismo criterio que Value.cpp):
//
//   offset 0   [nSlots:u16][dataStart:u16][dataEnd:u16][freeHead:u16]   cabecera, 8 B
//   offset 8   directorio: 4 B por slot, [offset:u16][length:u16]
//              dataStart = fin del directorio = 8 + 4*nSlots
//   ...hueco libre (dataEnd - dataStart)...
//   ...datos, creciendo HACIA ATRAS desde page_size...
//
// El puntero de la cadena de libres vive en los 2 PRIMEROS bytes de la region liberada, y
// no en el directorio. Asi el directorio de un slot libre conserva offset y length
// intactos: freeSpace puede sumar los length libres y el first-fit puede medir cada region
// sin desandar la cadena para recalcular nada.
#include "storage/SlottedPage.h"

#include <cassert>
#include <cstdint>

namespace {

// 0xFFFF es a la vez "no hay" de fin de cadena y "ningun slot libre". Es el centinela que
// fija el contrato, y coincide con el valor maximo de un SlotID: por eso nSlots nunca
// puede LLEGAR a 0xFFFF (si lo alcanzara, el slot 0xFFFF seria ambiguo).
constexpr uint16_t kNoSlot = 0xFFFF;

constexpr size_t kHeaderSize = 8;   // los 4 u16 de la cabecera
constexpr size_t kDirEntry   = 4;   // [offset:u16][length:u16]

// Los u16 se escriben y se leen a mano, igual que en Value.cpp: nada de memcpy, para que
// el archivo sea el mismo en cualquier endianness. No hay un header compartido de
// utilerias porque el contrato congela los 13 headers de §4 y agregar uno mas seria
// cambiar el contrato; son 3 lineas, y por eso se repiten en vez de inventar un sitio.
void putU16LE(Page& p, size_t pos, uint16_t v) {
    p[pos]     = static_cast<uint8_t>(v & 0xFF);
    p[pos + 1] = static_cast<uint8_t>((v >> 8) & 0xFF);
}

uint16_t getU16LE(const Page& p, size_t pos) {
    return static_cast<uint16_t>(static_cast<uint16_t>(p[pos]) |
                                 (static_cast<uint16_t>(p[pos + 1]) << 8));
}

uint16_t nSlotsOf(const Page& p)    { return getU16LE(p, 0); }
uint16_t dataStartOf(const Page& p) { return getU16LE(p, 2); }
uint16_t dataEndOf(const Page& p)   { return getU16LE(p, 4); }
uint16_t freeHeadOf(const Page& p)  { return getU16LE(p, 6); }

size_t dirPos(SlotID s)                    { return kHeaderSize + kDirEntry * s; }
uint16_t offsetAt(const Page& p, SlotID s) { return getU16LE(p, dirPos(s)); }
uint16_t lengthAt(const Page& p, SlotID s) { return getU16LE(p, dirPos(s) + 2); }

// El "siguiente libre" se lee de la region, no del directorio (ver la nota del layout).
uint16_t nextFreeAt(const Page& p, SlotID s) { return getU16LE(p, offsetAt(p, s)); }

// Recorre la cadena desde `head`. El techo de `nSlots` pasos no es paranoia decorativa: una
// pagina hecha a mano en un test puede tener un ciclo, y un recorrido colgado no dice
// NADA del bug. Con el techo, el peor caso es que un ciclo se corte solo y las funciones
// devuelvan un Status en vez de dejar la suite colgada.
template <typename F>
void forEachFree(const Page& p, SlotID head, F fn) {
    size_t    budget = nSlotsOf(p);
    SlotID    s      = head;
    for (; s != kNoSlot && budget > 0; s = nextFreeAt(p, s), --budget) fn(s);
}

bool chainContains(const Page& p, SlotID slot) {
    bool found = false;
    forEachFree(p, freeHeadOf(p), [&](SlotID s) {
        if (s == slot) found = true;
    });
    return found;
}

}  // namespace

SlottedPage SlottedPage::init(size_t page_size) {
    // 8 de cabecera + 4 de un slot + 2 de la tupla minima: por debajo de esto no hay
    // pagina valida, y el `size - 12` de maxTupleSize daria negativo.
    assert(page_size >= kHeaderSize + kDirEntry + 2);

    Page p(page_size, 0);
    putU16LE(p, 0, 0);                  // nSlots: todavia no hay ningun slot
    putU16LE(p, 2, static_cast<uint16_t>(kHeaderSize));  // dataStart: fin del directorio vacio
    putU16LE(p, 4, static_cast<uint16_t>(page_size));    // dataEnd: el final de la pagina
    putU16LE(p, 6, kNoSlot);            // freeHead: cadena vacia

    SlottedPage sp;
    sp.page_ = std::move(p);
    return sp;
}

SlottedPage SlottedPage::wrap(const Page& p) {
    assert(p.size() >= kHeaderSize + kDirEntry);

    SlottedPage sp;
    sp.page_ = p;
    return sp;
}

Status SlottedPage::insert(const std::vector<uint8_t>& bytes, SlotID& out_slot) {
    // El unico productor real es Tuple::serializeTo, que garantiza 4 B, pero el contrato
    // acepta bytes crudos: sin este assert un llamador podria meter 1 B, y al liberar esa
    // region no habria lugar para el puntero de 2 B de la cadena, que quedaria partido.
    // Es un assert y no un Status porque es error de programacion, no un dato de entrada
    // (regla 21, §7).
    assert(bytes.size() >= 2);

    if (bytes.size() > maxTupleSize(page_.size())) return Status::TupleTooLarge;

    const uint16_t len = static_cast<uint16_t>(bytes.size());

    // --- First-fit por la cadena de libres ---------------------------------
    // Cambio 43: los dos punteros de la cadena son slotID, no offsets. Por eso recorrerla
    // NO cuesta resolver offset -> slot en cada paso: es O(k) y no O(k*k).
    SlotID prev = kNoSlot;
    SlotID s    = freeHeadOf(page_);
    for (size_t budget = nSlotsOf(page_); s != kNoSlot && budget > 0; --budget) {
        const uint16_t next = nextFreeAt(page_, s);   // se lee ANTES de escribir: el
                                                       // puntero vive en los 2 primeros
                                                       // bytes de la region, que son
                                                       // justo los que va a pisar la tupla
        if (lengthAt(page_, s) >= len) {
            const uint16_t off = offsetAt(page_, s);
            for (size_t i = 0; i < bytes.size(); ++i) page_[off + i] = bytes[i];

            // El length NO baja (cambio 41): la region sigue siendo de `length` B, la
            // tupla nueva deja cola y lookup devuelve la region entera.
            if (prev == kNoSlot) {
                putU16LE(page_, 6, next);                       // era la cabeza
            } else {
                putU16LE(page_, offsetAt(page_, prev), next);    // la saltea
            }
            out_slot = s;
            return Status::Ok;
        }
        prev = s;
        s    = next;
    }

    // --- No hay region libre que sirva: escribir al final -------------------
    const uint16_t n = nSlotsOf(page_);
    if (n >= kNoSlot) return Status::PageFull;   // no queda un slotID libre

    // El +4 que casi nadie suma: escribir al final tambien cuesta la entrada de
    // directorio nueva, que empuja dataStart 4 B hacia arriba. Con aritmetica u16, el
    // `dataStart + 4` de una pagina llena (dataStart == dataEnd) da la vuelta y el
    // chequeo pasaria, asi que la resta va en signed.
    const int64_t hueco = static_cast<int64_t>(dataEndOf(page_)) -
                          (static_cast<int64_t>(dataStartOf(page_)) + static_cast<int64_t>(kDirEntry));
    if (hueco < static_cast<int64_t>(len)) return Status::PageFull;

    const uint16_t off = static_cast<uint16_t>(dataEndOf(page_) - len);
    for (size_t i = 0; i < bytes.size(); ++i) page_[off + i] = bytes[i];

    putU16LE(page_, dirPos(n), off);
    putU16LE(page_, dirPos(n) + 2, len);
    putU16LE(page_, 0, static_cast<uint16_t>(n + 1));                              // nSlots
    putU16LE(page_, 2, static_cast<uint16_t>(kHeaderSize + kDirEntry * (n + 1)));  // dataStart
    putU16LE(page_, 4, off);                                                      // dataEnd

    out_slot = n;
    return Status::Ok;
}

Status SlottedPage::lookup(SlotID slot, std::vector<uint8_t>& out) const {
    if (slot >= nSlotsOf(page_)) return Status::NotFound;
    // Un slot libre NO tiene tupla. La freesness se define por la pertenencia a la cadena
    // (el directorio de un slot libre conserva offset y length intactos, y solo hay un
    // modo de distinguirlos); el recorrido es O(k), igual que el first-fit del insert.
    if (chainContains(page_, slot)) return Status::NotFound;

    const uint16_t off = offsetAt(page_, slot);
    const uint16_t len = lengthAt(page_, slot);
    // La region COMPLETA segun el directorio. Al reutilizar, `len` sigue siendo el de la
    // region liberada y puede traer cola: quien llame, Tuple::deserialize, la ignora
    // (cambio 41).
    out.assign(page_.begin() + off, page_.begin() + off + len);
    return Status::Ok;
}

Status SlottedPage::erase(SlotID slot) {
    if (slot >= nSlotsOf(page_)) return Status::NotFound;
    if (chainContains(page_, slot)) return Status::NotFound;   // ya estaba libre

    const uint16_t off = offsetAt(page_, slot);
    // Enlaza la region liberada en la cabeza de la cadena, escribiendo el puntero en sus 2
    // primeros bytes. NO compacta: no se mueve un byte de datos, asi que el offset (y por
    // lo tanto el RowID) de las demas tuplas no cambia nunca.
    putU16LE(page_, off, freeHeadOf(page_));
    putU16LE(page_, 6, slot);
    return Status::Ok;
}

size_t SlottedPage::freeSpace() const {
    // (dataEnd - dataStart) + Σ length de los slots libres. dataEnd >= dataStart por el
    // invariante que mantiene insert (nunca escribe si el hueco no alcanza para los datos
    // mas la entrada nueva), asi que la resta u16 no da la vuelta.
    size_t n = static_cast<size_t>(dataEndOf(page_)) - dataStartOf(page_);
    forEachFree(page_, freeHeadOf(page_),
                [&](SlotID s) { n += lengthAt(page_, s); });
    return n;
}

size_t SlottedPage::slotCount() const { return nSlotsOf(page_); }

size_t SlottedPage::freeSlotCount() const {
    size_t n = 0;
    forEachFree(page_, freeHeadOf(page_), [&](SlotID) { ++n; });
    return n;
}

size_t SlottedPage::maxTupleSize(size_t page_size) {
    // 8 de cabecera + 4 de UNA entrada de directorio: la tupla mas grande que puede existir
    // alguna vez es la que ocupa la pagina entera sola. Con 4096 da 4084.
    return (page_size < kHeaderSize + kDirEntry) ? 0 : page_size - kHeaderSize - kDirEntry;
}

Page SlottedPage::toPage() const { return page_; }
