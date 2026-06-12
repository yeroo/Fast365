#include "zip.h"

#include <algorithm>

#include "inflate.h"

namespace {

constexpr uint32_t kEocdSig = 0x06054b50;    // end of central directory
constexpr uint32_t kCentralSig = 0x02014b50; // central directory header
constexpr uint32_t kLocalSig = 0x04034b50;   // local file header

uint16_t rd16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

uint32_t rd32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

} // namespace

bool ZipArchive::open(const uint8_t* data, size_t size) {
    m_data = data;
    m_size = size;
    m_entries.clear();
    if (size < 22) return false;

    // The EOCD record sits at the very end, preceded by an up-to-64KB comment.
    size_t maxBack = std::min(size, static_cast<size_t>(22 + 65535));
    size_t stop = size - maxBack;
    size_t off = size - 22;
    for (;;) {
        if (rd32(data + off) == kEocdSig) break;
        if (off == stop) return false;
        off--;
    }

    uint16_t count = rd16(data + off + 10);
    uint32_t cdOffset = rd32(data + off + 16);

    size_t p = cdOffset;
    m_entries.reserve(count);
    for (int i = 0; i < count; i++) {
        if (p + 46 > size || rd32(data + p) != kCentralSig) return false;
        ZipEntry e;
        e.method = rd16(data + p + 10);
        e.compSize = rd32(data + p + 20);
        e.uncompSize = rd32(data + p + 24);
        uint16_t nameLen = rd16(data + p + 28);
        uint16_t extraLen = rd16(data + p + 30);
        uint16_t commentLen = rd16(data + p + 32);
        e.localOffset = rd32(data + p + 42);
        if (p + 46 + nameLen > size) return false;
        e.name.assign(reinterpret_cast<const char*>(data + p + 46), nameLen);
        m_entries.push_back(std::move(e));
        p += 46u + nameLen + extraLen + commentLen;
    }
    return true;
}

const ZipEntry* ZipArchive::find(std::string_view name) const {
    for (const auto& e : m_entries)
        if (e.name == name) return &e;
    return nullptr;
}

bool ZipArchive::extract(const ZipEntry& entry, std::vector<uint8_t>& out) const {
    size_t p = entry.localOffset;
    if (p + 30 > m_size || rd32(m_data + p) != kLocalSig) return false;
    // Sizes in the local header may be zero (streaming flag); always trust
    // the central directory values instead.
    uint16_t nameLen = rd16(m_data + p + 26);
    uint16_t extraLen = rd16(m_data + p + 28);
    size_t dataOffset = p + 30u + nameLen + extraLen;
    if (dataOffset + entry.compSize > m_size) return false;
    const uint8_t* src = m_data + dataOffset;

    out.clear();
    if (entry.method == 0) { // stored
        if (entry.compSize != entry.uncompSize) return false;
        out.assign(src, src + entry.compSize);
        return true;
    }
    if (entry.method == 8) { // deflate
        return inflateRaw(src, entry.compSize, out, entry.uncompSize) &&
               out.size() == entry.uncompSize;
    }
    return false;
}
