// Loading the eApp executable into guest memory.
//
// The file is a flat image that loads at IMAGE_BASE; only its header needs parsing here, and
// only for one thing: the vector table at +0x14, whose entries are the absolute addresses the
// firmware calls (init vectors once, then the last one every frame). The layout was established
// in the emulator's loader (reference/eapp-loader/lib.rs, `EApp::parse`); the Python tools carry
// a fuller parser (tools/recomp/image.py) for the framework descriptors.
#pragma once

#include <cstdint>
#include <vector>

namespace minigolf {

struct EAppImage {
    uint32_t load_base = 0;
    uint32_t size = 0;              // bytes of file content; the BSS span follows, zero-filled
    std::vector<uint32_t> vectors;  // non-zero entries of the vector table, in order
};

// Read `path`, check the magic, copy the image into guest memory at IMAGE_BASE, and return what
// the frame pump needs to know. Fatal on any failure: without the image there is nothing to run.
EAppImage load_eapp_image(const char* path);

}  // namespace minigolf
