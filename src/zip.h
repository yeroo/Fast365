#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

struct ZipEntry {
    std::string name; // forward-slash path as stored in the archive
    uint32_t method = 0;
    uint32_t compSize = 0;
    uint32_t uncompSize = 0;
    uint32_t localOffset = 0;
};

// Read-only ZIP reader over an in-memory buffer. The buffer must outlive
// the archive. Supports stored and deflate entries (all a DOCX ever uses).
class ZipArchive {
public:
    bool open(const uint8_t* data, size_t size);
    const ZipEntry* find(std::string_view name) const;
    bool extract(const ZipEntry& entry, std::vector<uint8_t>& out) const;
    const std::vector<ZipEntry>& entries() const { return m_entries; }

private:
    const uint8_t* m_data = nullptr;
    size_t m_size = 0;
    std::vector<ZipEntry> m_entries;
};
