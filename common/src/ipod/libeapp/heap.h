// The guest heap behind miscTBD #0 (alloc) and #1 (free).
//
// The allocator is a deliberate copy of the emulator's (reference/eapp-loader/lib.rs, `alloc` and
// `free`), because the verification oracle compares guest addresses: every pointer the game
// receives must be the same pointer the emulator handed out for the same sequence of requests.
// The algorithm is therefore fixed, not a design choice:
//
//   * requests are rounded up to 8 bytes and carry an 8-byte header holding the block's total size;
//   * the heap grows by bumping a pointer from HEAP_BASE, and freed blocks go on a first-fit free
//     list that is never coalesced;
//   * a recycled block is zeroed, because games check fresh memory for "unset" fields (Bejeweled
//     read a stale id from a recycled block and hit an assert);
//   * a request that does not fit returns 0, which is the failure value the games test for.
#pragma once

#include <cstdint>
#include <vector>

namespace ipod::eapp {

class Heap {
public:
    // Returns a guest pointer to at least `size` bytes, or 0 when the heap is exhausted.
    [[nodiscard]] uint32_t alloc(uint32_t size);

    // Releases a block from `alloc`. Pointers outside the heap are ignored, as the emulator does.
    void free(uint32_t pointer);

    // Resizes a block from `alloc`, keeping its contents, and returns where it now lives — the
    // `miscTBD #2` behind C's `realloc`. Its rules are the emulator's (`Stub::Realloc`), and
    // like the allocator's they are fixed rather than chosen, because the address that comes
    // back is compared by the oracle:
    //
    //   * a null block is `alloc(size)`; a zero size is `free(block)` and answers 0;
    //   * a block the heap never handed out has no header to read, so a fresh block is
    //     allocated and nothing is copied;
    //   * a block whose rounded payload already holds `size` bytes is answered **in place**.
    //     Not an optimisation: the games grow strings one byte at a time through this call
    //     (Vortex appends every character of its 8 KB string table that way), and a realloc that
    //     always moved made each append an allocate, a copy and a free over a linearly scanned
    //     free list — the whole parse went quadratic and never finished;
    //   * otherwise a new block is allocated, the smaller of the two payloads copied, and the old
    //     block freed — in that order, so the new block never recycles the old one.
    [[nodiscard]] uint32_t realloc(uint32_t pointer, uint32_t size);

    // Bytes handed out so far, counting headers and never-recycled blocks. For diagnostics.
    [[nodiscard]] uint32_t used() const;

private:
    struct FreeBlock {
        uint32_t block;  // address of the header
        uint32_t size;   // total size including the header
    };

    static constexpr uint32_t HEADER_SIZE = 8;

    uint32_t next_ = 0;  // first never-used address; set on first use
    std::vector<FreeBlock> free_list_;
};

// The one heap the guest sees. Accessor rather than a global so its lifetime is explicit.
Heap& heap();

}  // namespace ipod::eapp
