#include "ipod/gamedata/zip.h"

#include <cstdio>
#include <cstring>
#include <memory>
#include <zlib.h>

namespace ipod::gamedata {

namespace {

constexpr uint32_t END_OF_CENTRAL_DIRECTORY = 0x0605'4b50;
constexpr uint32_t CENTRAL_FILE_HEADER = 0x0201'4b50;
constexpr uint32_t LOCAL_FILE_HEADER = 0x0403'4b50;
constexpr size_t END_RECORD_SIZE = 22;
constexpr size_t MAX_COMMENT = 0xffff;
constexpr uint16_t METHOD_STORED = 0, METHOD_DEFLATED = 8;
constexpr uint16_t FLAG_ENCRYPTED = 1;

uint16_t read16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

uint32_t read32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

struct FileCloser {
    void operator()(std::FILE* file) const { std::fclose(file); }
};
using File = std::unique_ptr<std::FILE, FileCloser>;

bool read_at(std::FILE* file, long offset, std::vector<uint8_t>& buffer, size_t size) {
    buffer.resize(size);
    return std::fseek(file, offset, SEEK_SET) == 0 &&
           std::fread(buffer.data(), 1, size, file) == size;
}

}  // namespace

bool ZipArchive::open(const std::string& path, std::string& error) {
    path_ = path;
    entries_.clear();
    File file(std::fopen(path.c_str(), "rb"));
    if (!file) {
        error = "cannot open " + path;
        return false;
    }
    std::fseek(file.get(), 0, SEEK_END);
    const long file_size = std::ftell(file.get());
    if (file_size < static_cast<long>(END_RECORD_SIZE)) {
        error = "not a zip archive (too small)";
        return false;
    }

    // The end record sits at the very end, possibly followed by a comment: scan back for it.
    const size_t tail_size = static_cast<size_t>(file_size) < END_RECORD_SIZE + MAX_COMMENT
                                 ? static_cast<size_t>(file_size)
                                 : END_RECORD_SIZE + MAX_COMMENT;
    std::vector<uint8_t> tail;
    if (!read_at(file.get(), file_size - static_cast<long>(tail_size), tail, tail_size)) {
        error = "cannot read the end of " + path;
        return false;
    }
    size_t end_record = tail_size;  // "not found"
    for (size_t i = tail_size - END_RECORD_SIZE + 1; i-- > 0;) {
        if (read32(&tail[i]) == END_OF_CENTRAL_DIRECTORY) {
            end_record = i;
            break;
        }
    }
    if (end_record == tail_size) {
        error = "not a zip archive (no end record)";
        return false;
    }
    const uint8_t* end = &tail[end_record];
    const uint16_t entry_count = read16(end + 10);
    const uint32_t directory_size = read32(end + 12);
    const uint32_t directory_offset = read32(end + 16);
    if (entry_count == 0xffff || directory_offset == 0xffff'ffffu) {
        error = "zip64 archives are not supported";
        return false;
    }

    std::vector<uint8_t> directory;
    if (!read_at(file.get(), static_cast<long>(directory_offset), directory, directory_size)) {
        error = "cannot read the central directory";
        return false;
    }
    size_t at = 0;
    for (uint16_t i = 0; i < entry_count; ++i) {
        if (at + 46 > directory.size() || read32(&directory[at]) != CENTRAL_FILE_HEADER) {
            error = "damaged central directory";
            return false;
        }
        const uint8_t* header = &directory[at];
        const uint16_t flags = read16(header + 8);
        const uint16_t name_length = read16(header + 28);
        const uint16_t extra_length = read16(header + 30);
        const uint16_t comment_length = read16(header + 32);
        if (at + 46 + name_length > directory.size()) {
            error = "damaged central directory";
            return false;
        }
        ZipEntry entry;
        entry.name.assign(reinterpret_cast<const char*>(header + 46), name_length);
        entry.method = read16(header + 10);
        entry.crc32 = read32(header + 16);
        entry.compressed_size = read32(header + 20);
        entry.size = read32(header + 24);
        entry.local_header_offset = read32(header + 42);
        if ((flags & FLAG_ENCRYPTED) != 0) {
            error = "encrypted archives are not supported";
            return false;
        }
        entries_.push_back(std::move(entry));
        at += 46u + name_length + extra_length + comment_length;
    }
    return true;
}

bool ZipArchive::read(const ZipEntry& entry, std::vector<uint8_t>& out, std::string& error) const {
    File file(std::fopen(path_.c_str(), "rb"));
    if (!file) {
        error = "cannot open " + path_;
        return false;
    }
    std::vector<uint8_t> local;
    if (!read_at(file.get(), static_cast<long>(entry.local_header_offset), local, 30) ||
        read32(local.data()) != LOCAL_FILE_HEADER) {
        error = "damaged entry " + entry.name;
        return false;
    }
    const long data_offset =
        static_cast<long>(entry.local_header_offset) + 30 + read16(&local[26]) + read16(&local[28]);
    std::vector<uint8_t> compressed;
    if (!read_at(file.get(), data_offset, compressed, entry.compressed_size)) {
        error = "truncated entry " + entry.name;
        return false;
    }

    if (entry.method == METHOD_STORED) {
        out = std::move(compressed);
    } else if (entry.method == METHOD_DEFLATED) {
        out.resize(entry.size);
        z_stream stream{};
        if (inflateInit2(&stream, -MAX_WBITS) != Z_OK) {  // raw deflate: no zlib header
            error = "zlib initialisation failed";
            return false;
        }
        stream.next_in = compressed.data();
        stream.avail_in = static_cast<uInt>(compressed.size());
        stream.next_out = out.data();
        stream.avail_out = static_cast<uInt>(out.size());
        const int status = inflate(&stream, Z_FINISH);
        inflateEnd(&stream);
        if (status != Z_STREAM_END || stream.total_out != entry.size) {
            error = "damaged deflate data in " + entry.name;
            return false;
        }
    } else {
        error = "unsupported compression method in " + entry.name;
        return false;
    }
    if (out.size() != entry.size ||
        ::crc32(0, out.data(), static_cast<uInt>(out.size())) != entry.crc32) {
        error = "checksum mismatch inside the archive for " + entry.name;
        return false;
    }
    return true;
}

}  // namespace ipod::gamedata
