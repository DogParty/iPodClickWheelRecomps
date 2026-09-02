// The typed-text inbox (src/platform/text_entry.{h,cpp}): the portable half of typing a name.
//
// The iPod had no keyboard, so the game spells a name out on the wheel. A platform that does
// have one hands what was typed to this inbox each frame, and the name entry takes it. Only
// platforms that say so take part, which is what `text_entry_supported` is for; everything else
// here is the plain arithmetic of a one-frame mailbox, and these tests pin it.
#include "platform/text_entry.h"

#include <cstdio>
#include <string>

namespace {

using namespace holdem::platform;

int failures = 0;

void check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

TypedText letters(const std::string& text) {
    TypedText typed;
    typed.characters = text;
    return typed;
}

void test_support() {
    check(!text_entry_supported(), "a platform takes no typing until it says it can");
    set_text_entry_supported(true);
    check(text_entry_supported(), "and does once it has");
}

void test_empty() {
    TypedText typed;
    check(typed.empty(), "nothing typed is empty");
    typed.backspaces = 1;
    check(!typed.empty(), "a backspace alone is not");
    typed = TypedText{};
    typed.confirm = true;
    check(!typed.empty(), "nor is a bare Return");
}

void test_delivery() {
    (void)text_entry_take();  // start from a clear inbox

    text_entry_deliver(letters("AB"));
    text_entry_deliver(letters("C"));
    TypedText taken = text_entry_take();
    check(taken.characters == "ABC", "what arrives across frames is taken in order");
    check(text_entry_take().empty(), "and taking it empties the inbox");

    TypedText typed = letters("Z");
    typed.backspaces = 2;
    typed.confirm = true;
    text_entry_deliver(typed);
    taken = text_entry_take();
    check(taken.characters == "Z" && taken.backspaces == 2 && taken.confirm,
          "backspaces and Return travel with the letters");
    taken = text_entry_take();
    check(taken.backspaces == 0 && !taken.confirm, "and none of it is delivered twice");
}

void test_bounded() {
    (void)text_entry_take();

    // Typing while no screen is reading must not pile up into one enormous burst later.
    for (int frame = 0; frame < 200; ++frame) {
        text_entry_deliver(letters("x"));
    }
    const TypedText taken = text_entry_take();
    check(taken.characters.size() <= 64, "an unread inbox stays bounded");
    check(!taken.characters.empty() && taken.characters.back() == 'x', "keeping the latest keys");
}

}  // namespace

int main() {
    test_support();
    test_empty();
    test_delivery();
    test_bounded();
    if (failures != 0) {
        std::fprintf(stderr, "text entry: %d failure(s)\n", failures);
        return 1;
    }
    std::puts("text entry: ok");
    return 0;
}
