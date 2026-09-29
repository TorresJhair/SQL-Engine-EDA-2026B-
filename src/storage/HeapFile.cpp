#include "storage/HeapFile.h"

#include "storage/SlottedPage.h"

#include <stdexcept>
#include <utility>

HeapFile::HeapFile(PageManager& pm) : pm_(pm), record_count_(pm.readMeta().record_count) {
    if (pm_.pageCount() > 1) lastInsertPageID_ = pm_.pageCount() - 1;
}

Status HeapFile::insert(const Tuple& tuple, RowID& out) {
    std::vector<uint8_t> bytes;
    const Status serialized = tuple.serializeTo(bytes);
    if (serialized != Status::Ok) return serialized;
    if (bytes.size() > SlottedPage::maxTupleSize(pm_.pageSize())) return Status::TupleTooLarge;

    PageID target = lastInsertPageID_;
    SlottedPage page = target == 0 ? SlottedPage::init(pm_.pageSize())
                                   : SlottedPage::wrap(pm_.read(target));
    SlotID slot = 0;
    Status inserted = page.insert(bytes, slot);
    if (inserted == Status::PageFull && target != 0) {
        target = pm_.allocate();
        page = SlottedPage::init(pm_.pageSize());
        inserted = page.insert(bytes, slot);
    } else if (target == 0 && inserted == Status::PageFull) {
        return Status::TupleTooLarge;
    }
    if (inserted != Status::Ok) return inserted;

    if (target == 0) target = pm_.allocate();
    pm_.write(target, page.toPage());
    lastInsertPageID_ = target;
    ++record_count_;
    out = {target, slot};
    return Status::Ok;
}

Status HeapFile::get(const RowID& rid, Tuple& out) {
    if (rid.pageID == 0 || rid.pageID >= pm_.pageCount()) return Status::NotFound;
    const Page bytes = pm_.read(rid.pageID);
    const SlottedPage page = SlottedPage::wrap(bytes);
    std::vector<uint8_t> tuple_bytes;
    const Status found = page.lookup(rid.slotID, tuple_bytes);
    if (found != Status::Ok) return found;
    return Tuple::deserialize(tuple_bytes.data(), tuple_bytes.size(), out);
}

void HeapFile::flush() {
    FileMeta meta = pm_.readMeta();
    meta.record_count = record_count_;
    pm_.writeMeta(meta);
    pm_.flush();
}

HeapFile::Scan::Scan() = default;
HeapFile::Scan::Scan(PageManager* pm) : pm_(pm) { reset(); }

bool HeapFile::Scan::next(Tuple& out, RowID& rid) {
    if (pm_ == nullptr) return false;
    while (page_id_ < pm_->pageCount()) {
        if (!page_loaded_) {
            current_page_ = pm_->read(page_id_);
            page_loaded_ = true;
            slot_id_ = 0;
        }
        const SlottedPage page = SlottedPage::wrap(current_page_);
        while (slot_id_ < page.slotCount()) {
            const SlotID slot = slot_id_++;
            std::vector<uint8_t> tuple_bytes;
            const Status found = page.lookup(slot, tuple_bytes);
            if (found == Status::NotFound) continue;
            if (found != Status::Ok) throw std::runtime_error("HeapFile::Scan: invalid slot");
            const Status decoded = Tuple::deserialize(tuple_bytes.data(), tuple_bytes.size(), out);
            if (decoded != Status::Ok) throw std::runtime_error("HeapFile::Scan: corrupt tuple");
            rid = {page_id_, slot};
            return true;
        }
        ++page_id_;
        page_loaded_ = false;
    }
    return false;
}

void HeapFile::Scan::reset() {
    page_id_ = 1;
    slot_id_ = 0;
    current_page_.clear();
    page_loaded_ = false;
}

HeapFile::Scan HeapFile::scan() { return Scan(&pm_); }
PageID HeapFile::lastInsertPageID() const { return lastInsertPageID_; }
size_t HeapFile::recordCount() const { return static_cast<size_t>(record_count_); }
