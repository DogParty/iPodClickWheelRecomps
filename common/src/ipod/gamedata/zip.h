// A small reader for ordinary zip archives: the central directory, and entries stored or
// deflated. That is all a copy of the game's folder ever needs; zip64 and encryption are
// reported as unsupported rather than silently misread.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ipod::gamedata {

struct ZipEntry {
    std::string name;  // as stored, '/'-separated
    uint32_t crc32;
    uint32_t compressed_size;
    uint32_t size;
    uint16_t method;  // 0 stored, 8 deflated
    uint32_t local_header_offset;
};

class ZipArchive {
public:
    // Reads the central directory. On failure returns false and says why in `error`.
    bool open(const std::string& path, std::string& error);

    [[nodiscard]] const std::vector<ZipEntry>& entries() const { return entries_; }

    // Inflates one entry into `out`. False (with `error`) if the archive lies about it.
    bool read(const ZipEntry& entry, std::vector<uint8_t>& out, std::string& error) const;

private:
    std::string path_;
    std::vector<ZipEntry> entries_;
};

}  // namespace ipod::gamedata
