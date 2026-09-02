// The queues and buffers shared between the frame pump and the framework implementations:
// input samples and file completions. Declared in ipod_eapp.h and host_state.h.
#include "host_state.h"

#include "ipod_eapp.h"
#include "runtime/memory.h"

#include <deque>
#include <string>

namespace bowling::eapp {

namespace {

constexpr uint32_t INPUT_EVENT_PRESENT = 0x4000'0000u;

std::deque<uint32_t>& input_queue() {
    static std::deque<uint32_t> queue;
    return queue;
}

std::vector<uint32_t>& pending_completions() {
    static std::vector<uint32_t> queue;
    return queue;
}

}  // namespace

void queue_input(uint8_t code) {
    input_queue().push_back(INPUT_EVENT_PRESENT | code);
}

void clear_input_queue() {
    input_queue().clear();
}

// Used by the InputEvents implementation: the next event word, or 0 when nothing is queued.
uint32_t take_input_event() {
    if (input_queue().empty()) {
        return 0;
    }
    const uint32_t event = input_queue().front();
    input_queue().pop_front();
    return event;
}

// Used by the AsyncFileIO implementation: owe the game one completion for one operation.
void queue_completion(uint32_t request) {
    pending_completions().push_back(request);
}

std::vector<uint32_t> take_pending_completions() {
    std::vector<uint32_t> due;
    due.swap(pending_completions());
    return due;
}

std::string read_guest_string(uint32_t address, uint32_t max_length) {
    std::string text;
    for (uint32_t i = 0; i < max_length; ++i) {
        const uint32_t c = ld8(address + i);
        if (c == 0) {
            break;
        }
        text.push_back(static_cast<char>(c));
    }
    return text;
}

}  // namespace bowling::eapp
