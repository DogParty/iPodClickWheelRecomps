// The random numbers: a Mersenne Twister (with the game's own tempering masks) seeded from
// the clock at start-up, drawn by the hole objects' random waits and the ball's colour.
//
// This is the first piece of state that lives on the host rather than in guest memory: the
// original kept the generator in a 0x9d8-byte block (0x1803a9b4) and a second, global seed
// table (0x18040528); nothing else in the game reads either, and no framework is ever handed
// a pointer into them. Callers still pass the original's object address — it is an identity
// the hole objects store, not a place anything looks.
#include "random.h"

#include "calling.h"
#include "libc.h"
#include "runtime/memory.h"
#include "runtime/runtime.h"

namespace minigolf::game {

namespace {

constexpr uint32_t RANDOM_OBJECT = 0x1803'a9b4;  // the one generator's address in the original
constexpr uint32_t STATE_WORDS = 624, TWIST_OFFSET = 397, SEED_MULTIPLIER = 0x10dcd;
constexpr uint32_t MATRIX = 0x9908'b0df, TEMPER_B = 0x9d2c'56ff, TEMPER_C = 0xefc6'7fff;

// The generator: the twister's state, and the original's book-keeping — how many words remain
// before the state is twisted again (-1 when exhausted, below that when never seeded), the
// next word to draw, whether it was seeded, and the last number it gave.
struct Generator {
    uint32_t state[STATE_WORDS] = {};
    int32_t left = 0;
    uint32_t next = 0;  // index into `state`
    bool seeded = false;
    uint32_t seed = 0, last = 0;
};

Generator generator;
uint32_t global_seed_table[STATE_WORDS];  // 0x1800fddc fills this from the clock; unread since

uint32_t temper(uint32_t x) {
    x ^= x >> 11;
    x ^= (x << 7) & TEMPER_B;
    x ^= (x << 15) & TEMPER_C;
    return x ^ (x >> 18);
}

}  // namespace

// 0x1800fddc — the start-up seed: a second, global table filled the same way as a generator.
void random_seed(uint32_t seed) {
    global_seed_table[0] = seed;
    for (uint32_t i = 1; i < STATE_WORDS; ++i) {
        global_seed_table[i] = global_seed_table[i - 1] * SEED_MULTIPLIER;
    }
}

// 0x180084ec — seed the state: the seed made odd, then each word the last times a constant.
void random_object_seed(uint32_t object, uint32_t seed) {
    if (object == 0) {
        assert_trap(0x180084f8u);
    }
    uint32_t value = seed | 1;
    generator.state[0] = value;
    generator.left = 0;
    for (uint32_t i = 1; i < STATE_WORDS; ++i) {
        value *= SEED_MULTIPLIER;
        generator.state[i] = value;
    }
    generator.seeded = true;
    generator.seed = seed;
}

// 0x18008408 — the one random object, seeded.
uint32_t random_create(uint32_t seed) {
    generator = Generator{};
    random_object_seed(RANDOM_OBJECT, seed);
    return RANDOM_OBJECT;
}

// 0x18015d10 — regenerate the state and return its first word tempered. A state that was
// never seeded (LEFT below -1; an exhausted state sits at exactly -1) is seeded first.
uint32_t random_generate() {
    if (generator.left < -1) {
        random_object_seed(RANDOM_OBJECT, 0x1105);
    }
    generator.left = STATE_WORDS - 1;
    generator.next = 1;
    uint32_t* const s = generator.state;
    const auto twist = [&](uint32_t k, uint32_t far) {
        const uint32_t y = (s[k] & 0x8000'0000u) | (s[k + 1] & 0x7fff'ffffu);
        s[k] = s[far] ^ (y >> 1) ^ ((s[k + 1] & 1) != 0 ? MATRIX : 0);
    };
    for (uint32_t k = 0; k < STATE_WORDS - TWIST_OFFSET; ++k) {
        twist(k, k + TWIST_OFFSET);
    }
    for (uint32_t k = STATE_WORDS - TWIST_OFFSET; k < STATE_WORDS - 1; ++k) {
        twist(k, k + TWIST_OFFSET - STATE_WORDS);
    }
    const uint32_t y = (s[STATE_WORDS - 1] & 0x8000'0000u) | (s[0] & 0x7fff'ffffu);
    s[STATE_WORDS - 1] = s[TWIST_OFFSET - 1] ^ (y >> 1) ^ ((s[0] & 1) != 0 ? MATRIX : 0);
    return temper(s[0]);
}

// 0x18008454 — the next number below `modulus`, remembered in the object.
uint32_t random_next(uint32_t object, uint32_t modulus) {
    if (object == 0) {
        assert_trap(0x18008464u);
    }
    generator.left -= 1;
    uint32_t value;
    if (generator.left >= 0 || !generator.seeded) {
        if (generator.left < 0) {
            generator.left = STATE_WORDS - 1;
            generator.next = 1;
        }
        value = temper(generator.state[generator.next++]);
    } else {
        value = random_generate();
    }
    generator.last = libc::unsigned_divide(value, modulus).remainder;
    return generator.last;
}

}  // namespace minigolf::game
