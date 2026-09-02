// The typed-text inbox. See text_entry.h.
#include "ipod/platform/text_entry.h"

#include <algorithm>
#include <cstddef>

namespace ipod::platform {

namespace {

bool supported = false;
TypedText pending;

}  // namespace

bool text_entry_supported() {
    return supported;
}

void set_text_entry_supported(bool value) {
    supported = value;
}

// Nothing reads the inbox except a screen that takes typing, so text typed anywhere else would
// sit here and arrive all at once on the screen that does. Keeping only the tail of a short
// buffer bounds that: a burst within a frame survives whole, an afternoon of stray keys does not.
constexpr size_t INBOX_LIMIT = 64;

void text_entry_deliver(const TypedText& typed) {
    pending.characters += typed.characters;
    if (pending.characters.size() > INBOX_LIMIT) {
        pending.characters.erase(0, pending.characters.size() - INBOX_LIMIT);
    }
    pending.backspaces =
        std::min(pending.backspaces + typed.backspaces, static_cast<unsigned>(INBOX_LIMIT));
    pending.confirm = pending.confirm || typed.confirm;
}

TypedText text_entry_take() {
    TypedText taken;
    taken.characters.swap(pending.characters);
    taken.backspaces = pending.backspaces;
    taken.confirm = pending.confirm;
    pending.backspaces = 0;
    pending.confirm = false;
    return taken;
}

}  // namespace ipod::platform
