#include "storage/PageManager.h"

#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <utility>

namespace {
constexpr uint32_t kFileMagic = 0x50414745U;  // "PAGE"
constexpr size_t kMetaSize = 28;

void put32(Page& page, size_t offset, uint32_t value) {
    for (size_t i = 0; i < 4; ++i) {
        page[offset + i] = static_cast<uint8_t>((value >> (8U * i)) & 0xFFU);
    }
}

uint32_t get32(const Page& page, size_t offset) {
    uint32_t value = 0;
    for (size_t i = 0; i < 4; ++i) {
        value |= static_cast<uint32_t>(page[offset + i]) << (8U * i);
    }
    return value;
}

void put64(Page& page, size_t offset, uint64_t value) {
    for (size_t i = 0; i < 8; ++i) {
        page[offset + i] = static_cast<uint8_t>((value >> (8U * i)) & 0xFFU);
    }
}

uint64_t get64(const Page& page, size_t offset) {
    uint64_t value = 0;
    for (size_t i = 0; i < 8; ++i) {
        value |= static_cast<uint64_t>(page[offset + i]) << (8U * i);
    }
    return value;
}

Page encodeMeta(const FileMeta& meta, size_t page_size) {
    Page page(page_size, 0);
    put32(page, 0, meta.magic);
    put32(page, 4, meta.page_size);
    put32(page, 8, meta.page_count);
    put32(page, 12, meta.root_page_id);
    put32(page, 16, meta.height);
    put64(page, 20, meta.record_count);
    return page;
}

FileMeta decodeMeta(const Page& page) {
    return {get32(page, 0), get32(page, 4), get32(page, 8), get32(page, 12),
            get32(page, 16), get64(page, 20)};
}

std::streamoff offsetFor(PageID page_id, size_t page_size) {
    const uint64_t offset = static_cast<uint64_t>(page_id) * page_size;
    if (offset > static_cast<uint64_t>(std::numeric_limits<std::streamoff>::max())) {
        throw std::out_of_range("PageManager: page offset is too large");
    }
    return static_cast<std::streamoff>(offset);
}

void writeExact(const std::string& path, PageID page_id, const Page& page, size_t page_size) {
    std::fstream file(path, std::ios::binary | std::ios::in | std::ios::out);
    if (!file) throw std::runtime_error("PageManager: cannot open file for writing: " + path);
    file.seekp(offsetFor(page_id, page_size));
    file.write(reinterpret_cast<const char*>(page.data()), static_cast<std::streamsize>(page.size()));
    file.flush();
    if (!file) throw std::runtime_error("PageManager: failed to write page");
}
}  // namespace

Status PageManager::open(const std::string& path, size_t page_size, PageManager& out) {
    if (page_size < kMetaSize || page_size > std::numeric_limits<uint32_t>::max()) {
        return Status::Corrupt;
    }

    std::error_code exists_error;
    const bool exists = std::filesystem::exists(path, exists_error);
    if (exists_error) throw std::runtime_error("PageManager: cannot inspect file: " + path);
    if (!exists) {
        std::ofstream created(path, std::ios::binary | std::ios::trunc);
        if (!created) throw std::runtime_error("PageManager: cannot create file: " + path);
        PageManager fresh;
        fresh.path_ = path;
        fresh.page_size_ = page_size;
        fresh.page_count_ = 1;
        fresh.meta_ = {kFileMagic, static_cast<uint32_t>(page_size), 1, 0, 0, 0};
        Page page = encodeMeta(fresh.meta_, page_size);
        created.write(reinterpret_cast<const char*>(page.data()), static_cast<std::streamsize>(page.size()));
        created.flush();
        if (!created) throw std::runtime_error("PageManager: failed to initialize file metadata");
        out = std::move(fresh);
        return Status::Ok;
    }

    std::ifstream existing(path, std::ios::binary | std::ios::ate);
    if (!existing) throw std::runtime_error("PageManager: cannot open existing file: " + path);
    const std::streamoff length = existing.tellg();
    if (length < static_cast<std::streamoff>(page_size) || length % static_cast<std::streamoff>(page_size) != 0) {
        return Status::Corrupt;
    }
    existing.seekg(0);
    Page page(page_size);
    existing.read(reinterpret_cast<char*>(page.data()), static_cast<std::streamsize>(page.size()));
    if (!existing) return Status::Corrupt;
    const FileMeta meta = decodeMeta(page);
    const uint64_t actual_pages = static_cast<uint64_t>(length) / page_size;
    if (meta.magic != kFileMagic || meta.page_size != page_size || meta.page_count != actual_pages ||
        meta.page_count == 0 ||
        (meta.root_page_id != 0 && meta.root_page_id >= meta.page_count)) {
        return Status::Corrupt;
    }

    PageManager opened;
    opened.path_ = path;
    opened.page_size_ = page_size;
    opened.page_count_ = meta.page_count;
    opened.meta_ = meta;
    out = std::move(opened);
    return Status::Ok;
}

PageID PageManager::allocate() {
    if (page_count_ == std::numeric_limits<PageID>::max()) {
        throw std::overflow_error("PageManager: page ID space exhausted");
    }
    const PageID id = page_count_;
    const std::streamoff start = offsetFor(id, page_size_);
    std::fstream file(path_, std::ios::binary | std::ios::in | std::ios::out);
    if (!file) throw std::runtime_error("PageManager: cannot open file for allocation");
    file.seekp(start + static_cast<std::streamoff>(page_size_) - 1);
    const char zero = 0;
    file.write(&zero, 1);
    file.flush();
    if (!file) throw std::runtime_error("PageManager: failed to extend file");
    ++page_count_;
    meta_.page_count = page_count_;
    writeMeta(meta_);
    return id;
}

Page PageManager::read(PageID page_id) const {
    if (page_id == 0 || page_id >= page_count_) throw std::out_of_range("PageManager: invalid data page ID");
    std::ifstream file(path_, std::ios::binary);
    if (!file) throw std::runtime_error("PageManager: cannot open file for reading");
    file.seekg(offsetFor(page_id, page_size_));
    Page page(page_size_);
    file.read(reinterpret_cast<char*>(page.data()), static_cast<std::streamsize>(page.size()));
    if (!file) throw std::runtime_error("PageManager: failed to read page");
    ++page_reads_;
    return page;
}

void PageManager::write(PageID page_id, const Page& page) {
    if (page_id == 0 || page_id >= page_count_ || page.size() != page_size_) {
        throw std::invalid_argument("PageManager: invalid page ID or page size");
    }
    writeExact(path_, page_id, page, page_size_);
    ++page_writes_;
}

void PageManager::flush() {
    writeMeta(meta_);
}

FileMeta PageManager::readMeta() const {
    return meta_;
}

void PageManager::writeMeta(const FileMeta& meta) {
    FileMeta updated = meta;
    updated.magic = kFileMagic;
    updated.page_size = static_cast<uint32_t>(page_size_);
    updated.page_count = page_count_;
    meta_ = updated;
    writeExact(path_, 0, encodeMeta(meta_, page_size_), page_size_);
}

uint64_t PageManager::pageReads() const { return page_reads_; }
uint64_t PageManager::pageWrites() const { return page_writes_; }
size_t PageManager::pageSize() const { return page_size_; }
uint32_t PageManager::pageCount() const { return page_count_; }
void PageManager::resetCounters() const { page_reads_ = 0; page_writes_ = 0; }
