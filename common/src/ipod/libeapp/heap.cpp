// Guest heap allocator — see heap.h for why the algorithm is fixed.
#include "ipod/libeapp/heap.h"

#include "ipod/runtime/memory.h"

#include <algorithm>

namespace ipod::eapp {

uint32_t Heap::alloc(uint32_t size) {
    if (next_ == 0) {
        next_ = HEAP_BASE;
    }
    // Round up to eight bytes and add the header. A request within a few bytes of 4 GB would
    // wrap both sums and come back as a tiny block, so refuse anything the heap could never
    // hold before doing the arithmetic.
    if (size > HEAP_SIZE) {
        return 0;
    }
    const uint32_t rounded = (size + 7u) & ~7u;
    const uint32_t total = rounded + HEADER_SIZE;

    const auto fits = [total](const FreeBlock& candidate) { return candidate.size >= total; };
    const auto recycled = std::find_if(free_list_.begin(), free_list_.end(), fits);
    if (recycled != free_list_.end()) {
        const FreeBlock block = *recycled;
        free_list_.erase(recycled);
        st32(block.block, block.size);
        // Hand back clean memory: the previous owner's bytes are still there otherwise.
        for (uint32_t offset = 0; offset + 4 <= block.size - HEADER_SIZE; offset += 4) {
            st32(block.block + HEADER_SIZE + offset, 0);
        }
        return block.block + HEADER_SIZE;
    }

    if (next_ - HEAP_BASE + total > HEAP_SIZE) {
        return 0;  // refuse rather than hand back a pointer into nothing
    }
    const uint32_t block = next_;
    next_ += total;
    st32(block, total);
    return block + HEADER_SIZE;
}

void Heap::free(uint32_t pointer) {
    if (pointer < HEAP_BASE + HEADER_SIZE || pointer >= HEAP_BASE + HEAP_SIZE) {
        return;  // not ours; ignoring is safer than corrupting the list
    }
    const uint32_t block = pointer - HEADER_SIZE;
    const uint32_t size = ld32(block);
    const auto already_free = [block](const FreeBlock& entry) { return entry.block == block; };
    if (size >= HEADER_SIZE && std::none_of(free_list_.begin(), free_list_.end(), already_free)) {
        free_list_.push_back({block, size});
    }
}

uint32_t Heap::realloc(uint32_t pointer, uint32_t size) {
    if (pointer == 0) {
        return size == 0 ? 0 : alloc(size);
    }
    if (size == 0) {
        free(pointer);
        return 0;
    }
    const bool ours = pointer >= HEAP_BASE + HEADER_SIZE && pointer < HEAP_BASE + HEAP_SIZE;
    if (!ours) {
        return alloc(size);  // no header to trust, so nothing to copy either
    }
    const uint32_t old_total = ld32(pointer - HEADER_SIZE);
    const uint32_t old_payload = old_total >= HEADER_SIZE ? old_total - HEADER_SIZE : 0;
    if (old_payload >= size) {
        return pointer;  // already big enough: the common case for a growing string
    }
    const uint32_t moved = alloc(size);
    if (moved == 0) {
        return 0;  // the old block is left as it was, as realloc promises on failure
    }
    const uint32_t copied = std::min(old_payload, size);
    for (uint32_t offset = 0; offset < copied; ++offset) {
        st8(moved + offset, ld8(pointer + offset));
    }
    free(pointer);
    return moved;
}

uint32_t Heap::used() const {
    return next_ == 0 ? 0 : next_ - HEAP_BASE;
}

Heap& heap() {
    static Heap instance;
    return instance;
}

}  // namespace ipod::eapp
