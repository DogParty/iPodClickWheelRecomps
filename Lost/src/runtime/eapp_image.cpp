// eApp image loader. See eapp_image.h.
#include "runtime/eapp_image.h"

#include "runtime/memory.h"
#include "runtime/runtime.h"

#include <cstdio>
#include <cstring>

namespace lost {

namespace {

constexpr char EAPP_MAGIC[4] = {'e', 'a', 'p', 'p'};
constexpr uint32_t HEADER_VECTORS = 0x14;
constexpr uint32_t HEADER_VECTOR_COUNT = 5;
constexpr uint32_t HEADER_SIZE = HEADER_VECTORS + 4 * HEADER_VECTOR_COUNT;

std::vector<uint8_t> read_file(const char* path) {
    std::FILE* file = std::fopen(path, "rb");
    if (file == nullptr) {
        fatal("cannot open game image %s", path);
    }
    std::vector<uint8_t> bytes;
    uint8_t chunk[64 * 1024];
    for (;;) {
        const size_t got = std::fread(chunk, 1, sizeof chunk, file);
        bytes.insert(bytes.end(), chunk, chunk + got);
        if (got < sizeof chunk) {
            break;
        }
    }
    std::fclose(file);
    return bytes;
}

}  // namespace

EAppImage load_eapp_image(const char* path) {
    const std::vector<uint8_t> bytes = read_file(path);
    if (bytes.size() < HEADER_SIZE ||
        std::memcmp(bytes.data(), EAPP_MAGIC, sizeof EAPP_MAGIC) != 0) {
        fatal("%s is not an eApp image (bad magic or truncated header)", path);
    }
    if (bytes.size() > IMAGE_SPAN) {
        fatal("%s is %zu bytes, larger than the %u-byte image span", path, bytes.size(),
              IMAGE_SPAN);
    }

    EAppImage image;
    image.load_base = IMAGE_BASE;
    image.size = static_cast<uint32_t>(bytes.size());
    std::memcpy(guest_pointer(IMAGE_BASE, image.size), bytes.data(), bytes.size());

    for (uint32_t slot = 0; slot < HEADER_VECTOR_COUNT; ++slot) {
        const uint32_t vector = ld32(IMAGE_BASE + HEADER_VECTORS + 4 * slot);
        if (vector != 0) {
            image.vectors.push_back({slot, vector});
        }
    }
    if (image.vectors.empty()) {
        fatal("%s has an empty vector table", path);
    }
    return image;
}

}  // namespace lost
