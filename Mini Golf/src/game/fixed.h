// 16.16 fixed point — the arithmetic the game does all of its geometry in.
//
// A value is a 32-bit word: the high 16 bits are whole units (pixels, degrees, ball radii), the
// low 16 the fraction. The game stores them in guest memory as plain words and hands them to the
// pipeline as vertex data, so the words themselves are what the code passes around; `to_fixed`
// and `to_whole` are the conversions, and `Fixed16` is the checked type to do arithmetic in —
// it exists so that `a * b` cannot silently forget the `>> 16` that a raw multiply needs.
#pragma once

#include <cstdint>

namespace minigolf::game {

// Whole units to 16.16, and back. `to_whole` truncates towards negative infinity, as an
// arithmetic shift does, which is what the original's code relies on.
template <class Integer>
constexpr uint32_t to_fixed(Integer units) {
    return static_cast<uint32_t>(units) << 16;
}

// The same conversion where the surrounding arithmetic is signed — angles, screen deltas.
template <class Integer>
constexpr int32_t to_fixed_signed(Integer units) {
    return static_cast<int32_t>(to_fixed(units));
}

constexpr int32_t to_whole(uint32_t value) {
    return static_cast<int32_t>(value) >> 16;
}

constexpr int32_t to_whole(int32_t value) {
    return value >> 16;
}

// The fraction on its own, 0..0xffff.
constexpr uint32_t fraction_of(uint32_t value) {
    return value & 0xffffu;
}

// A 16.16 value with its arithmetic attached. Same size and representation as the word it wraps,
// so it can stand in a structure that mirrors guest memory.
class Fixed16 {
public:
    constexpr Fixed16() = default;

    static constexpr Fixed16 from_bits(uint32_t bits) {
        Fixed16 value;
        value.bits_ = static_cast<int32_t>(bits);
        return value;
    }
    static constexpr Fixed16 from_whole(int32_t units) {
        return from_bits(static_cast<uint32_t>(units) << 16);
    }

    [[nodiscard]] constexpr uint32_t bits() const { return static_cast<uint32_t>(bits_); }
    [[nodiscard]] constexpr int32_t whole() const { return bits_ >> 16; }

    constexpr Fixed16 operator+(Fixed16 other) const { return from_bits(bits() + other.bits()); }
    constexpr Fixed16 operator-(Fixed16 other) const { return from_bits(bits() - other.bits()); }
    constexpr Fixed16 operator-() const { return from_bits(0u - bits()); }

    // The product keeps 16 fractional bits, computed at 64 bits so nothing is lost on the way.
    constexpr Fixed16 operator*(Fixed16 other) const {
        const int64_t product = static_cast<int64_t>(bits_) * static_cast<int64_t>(other.bits_);
        return from_bits(static_cast<uint32_t>(static_cast<int32_t>(product >> 16)));
    }
    // Scaling by a whole number needs no shift.
    constexpr Fixed16 operator*(int32_t scale) const {
        return from_bits(static_cast<uint32_t>(bits_ * scale));
    }

    constexpr bool operator==(Fixed16 other) const { return bits_ == other.bits_; }
    constexpr bool operator!=(Fixed16 other) const { return bits_ != other.bits_; }
    constexpr bool operator<(Fixed16 other) const { return bits_ < other.bits_; }
    constexpr bool operator>(Fixed16 other) const { return bits_ > other.bits_; }
    constexpr bool operator<=(Fixed16 other) const { return bits_ <= other.bits_; }
    constexpr bool operator>=(Fixed16 other) const { return bits_ >= other.bits_; }

private:
    int32_t bits_ = 0;
};

static_assert(sizeof(Fixed16) == sizeof(uint32_t));

}  // namespace minigolf::game
