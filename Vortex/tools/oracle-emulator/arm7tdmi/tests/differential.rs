//! Differential tests against an independent oracle.
//!
//! The hand-written tests in `arm_state.rs` and `thumb_state.rs` were written by whoever wrote
//! the core, so they share its blind spots. These do not: every expected value here is computed
//! in **wider integer types**, where the answer is unambiguous and the code path has nothing in
//! common with the flag logic under test. `a + b` in `u64` cannot silently agree with a broken
//! 32-bit carry rule.
//!
//! Unicorn would be the conventional oracle, but its vendored QEMU does not compile under
//! current clang, and a JIT-based reference would have to stay a dev-only dependency anyway.
//! Widening arithmetic is a better oracle here: no dependency, and provably correct by
//! construction rather than by another implementation also being right.

use arm7tdmi::{Cpu, FlatMemory, Mode};

const CODE: u32 = 0x1000;
const ITERATIONS: usize = 20_000;

/// xorshift64*, fixed seed. Deterministic so a failure is reproducible from the seed alone.
struct Rng(u64);

impl Rng {
    fn next_u32(&mut self) -> u32 {
        self.0 ^= self.0 << 13;
        self.0 ^= self.0 >> 7;
        self.0 ^= self.0 << 17;
        (self.0 >> 32) as u32
    }

    /// Bias towards boundary values — flag bugs cluster at 0, 1, sign flips and all-ones,
    /// which uniform random 32-bit values almost never hit.
    fn interesting(&mut self) -> u32 {
        match self.next_u32() % 8 {
            0 => 0,
            1 => 1,
            2 => u32::MAX,
            3 => 0x8000_0000,
            4 => 0x7FFF_FFFF,
            5 => self.next_u32() & 0xFF,
            _ => self.next_u32(),
        }
    }
}

struct Outcome {
    result: u32,
    n: bool,
    z: bool,
    c: bool,
    v: bool,
}

/// Execute one ARM instruction with `r0 = a`, `r1 = b` and a chosen carry-in; report `r2`
/// and the resulting flags.
fn arm_op(instr: u32, a: u32, b: u32, carry: bool) -> Outcome {
    let mut mem = FlatMemory::new(0, 0x2000);
    mem.load(CODE, &instr.to_le_bytes());
    let mut cpu = Cpu::new();
    cpu.set_mode(Mode::System);
    cpu.regs[15] = CODE;
    cpu.regs[0] = a;
    cpu.regs[1] = b;
    cpu.cpsr.set_c(carry);
    cpu.step(&mut mem);
    Outcome {
        result: cpu.regs[2],
        n: cpu.cpsr.n(),
        z: cpu.cpsr.z(),
        c: cpu.cpsr.c(),
        v: cpu.cpsr.v(),
    }
}

/// The oracle: add-with-carry evaluated in 64-bit, where carry-out is "did it exceed 32 bits"
/// and signed overflow is "did the signed sum leave `i32` range". Both are definitional.
fn oracle_add(a: u32, b: u32, carry: bool) -> Outcome {
    let unsigned = a as u64 + b as u64 + carry as u64;
    let signed = a as i32 as i64 + b as i32 as i64 + carry as i64;
    let result = unsigned as u32;
    Outcome {
        result,
        n: result & 0x8000_0000 != 0,
        z: result == 0,
        c: unsigned > u32::MAX as u64,
        v: !(i32::MIN as i64..=i32::MAX as i64).contains(&signed),
    }
}

fn compare(name: &str, a: u32, b: u32, carry: bool, got: Outcome, want: Outcome) {
    let fmt = |o: &Outcome| {
        format!(
            "{:#010x} N={} Z={} C={} V={}",
            o.result, o.n as u8, o.z as u8, o.c as u8, o.v as u8
        )
    };
    assert!(
        got.result == want.result
            && got.n == want.n
            && got.z == want.z
            && got.c == want.c
            && got.v == want.v,
        "{name} a={a:#010x} b={b:#010x} carry_in={carry}\n  got  {}\n  want {}",
        fmt(&got),
        fmt(&want)
    );
}

#[test]
fn add_family_matches_widening_arithmetic() {
    let mut rng = Rng(0x2026_0811_A7DE);
    for _ in 0..ITERATIONS {
        let (a, b) = (rng.interesting(), rng.interesting());
        let carry = rng.next_u32() & 1 != 0;

        // ADDS r2, r0, r1 — carry-in is architecturally ignored.
        compare("ADDS", a, b, carry, arm_op(0xE090_2001, a, b, carry), oracle_add(a, b, false));

        // ADCS r2, r0, r1
        compare("ADCS", a, b, carry, arm_op(0xE0B0_2001, a, b, carry), oracle_add(a, b, carry));
    }
}

#[test]
fn subtract_family_matches_widening_arithmetic() {
    let mut rng = Rng(0xC0FF_EE00_1234_5678);
    for _ in 0..ITERATIONS {
        let (a, b) = (rng.interesting(), rng.interesting());
        let carry = rng.next_u32() & 1 != 0;

        // Subtraction is defined as a + !b + 1, so the same oracle covers it — which is
        // exactly the identity the implementation relies on, checked independently.
        compare("SUBS", a, b, carry, arm_op(0xE050_2001, a, b, carry), oracle_add(a, !b, true));
        compare("SBCS", a, b, carry, arm_op(0xE0D0_2001, a, b, carry), oracle_add(a, !b, carry));
        compare("RSBS", a, b, carry, arm_op(0xE070_2001, a, b, carry), oracle_add(b, !a, true));
        compare("RSCS", a, b, carry, arm_op(0xE0F0_2001, a, b, carry), oracle_add(b, !a, carry));
    }
}

#[test]
fn subtract_carry_is_exactly_the_no_borrow_predicate() {
    // An independent statement of the same rule: C set on SUB iff no borrow occurred,
    // i.e. iff a >= b as unsigned. Stated this way it shares nothing with the adder.
    let mut rng = Rng(0x5EED_5EED_5EED_5EED);
    for _ in 0..ITERATIONS {
        let (a, b) = (rng.interesting(), rng.interesting());
        let got = arm_op(0xE050_2001, a, b, false); // subs r2, r0, r1
        assert_eq!(
            got.c,
            a >= b,
            "SUBS {a:#010x} - {b:#010x}: C must mean 'no borrow'"
        );
        assert_eq!(got.result, a.wrapping_sub(b));
    }
}

// ---------------------------------------------------------------- shifts

/// Shift oracle in 64-bit. `u64`/`i64` shifts by 32 are well-defined, so the awkward
/// "shift by exactly the word size" cases need no special-casing here — which is precisely
/// where a 32-bit implementation is most likely to be wrong.
fn oracle_shift(ty: u32, val: u32, amount: u32, carry_in: bool) -> (u32, bool) {
    if amount == 0 {
        return (val, carry_in);
    }
    match ty {
        0 => {
            if amount > 32 {
                (0, false)
            } else {
                let wide = (val as u64) << amount;
                (wide as u32, (wide >> 32) & 1 != 0)
            }
        }
        1 => {
            if amount > 32 {
                (0, false)
            } else {
                let wide = val as u64;
                ((wide >> amount) as u32, (wide >> (amount - 1)) & 1 != 0)
            }
        }
        2 => {
            let n = amount.min(32);
            let wide = val as i32 as i64;
            (((wide >> n) as u32), ((wide >> (n - 1)) & 1) != 0)
        }
        _ => {
            let m = amount & 31;
            if m == 0 {
                (val, val >> 31 != 0)
            } else {
                let doubled = (val as u64) | ((val as u64) << 32);
                ((doubled >> m) as u32, (val >> (m - 1)) & 1 != 0)
            }
        }
    }
}

#[test]
fn register_shifts_match_widening_oracle() {
    // movs r2, r1, <ty> r0 — Rs = r0 supplies the amount, Rm = r1 the value.
    let encodings = [
        (0u32, 0xE1B0_2011u32, "LSL"),
        (1, 0xE1B0_2031, "LSR"),
        (2, 0xE1B0_2051, "ASR"),
        (3, 0xE1B0_2071, "ROR"),
    ];

    let mut rng = Rng(0xDEAD_BEEF_CAFE_BABE);
    for _ in 0..ITERATIONS {
        let value = rng.interesting();
        // Cover 0, the boundaries either side of 32, and the wider 8-bit field.
        let amount = match rng.next_u32() % 4 {
            0 => rng.next_u32() % 3,
            1 => 30 + rng.next_u32() % 5,
            2 => rng.next_u32() % 40,
            _ => rng.next_u32() & 0xFF,
        };
        let carry_in = rng.next_u32() & 1 != 0;

        for (ty, instr, name) in encodings {
            let got = arm_op(instr, amount, value, carry_in);
            let (want_result, want_carry) = oracle_shift(ty, value, amount, carry_in);
            assert_eq!(
                got.result, want_result,
                "{name} {value:#010x} by {amount}: result"
            );
            assert_eq!(
                got.c, want_carry,
                "{name} {value:#010x} by {amount}: carry"
            );
            assert_eq!(got.n, want_result & 0x8000_0000 != 0, "{name}: N");
            assert_eq!(got.z, want_result == 0, "{name}: Z");
        }
    }
}

// ---------------------------------------------------------------- cross-state agreement

/// Execute one Thumb instruction with `r0 = a`, `r1 = b`; report `r0` and the flags.
fn thumb_op(instr: u16, a: u32, b: u32, carry: bool) -> Outcome {
    let mut mem = FlatMemory::new(0, 0x2000);
    mem.load(CODE, &instr.to_le_bytes());
    let mut cpu = Cpu::new();
    cpu.set_mode(Mode::System);
    cpu.cpsr.set_thumb(true);
    cpu.regs[15] = CODE;
    cpu.regs[0] = a;
    cpu.regs[1] = b;
    cpu.cpsr.set_c(carry);
    cpu.step(&mut mem);
    Outcome {
        result: cpu.regs[0],
        n: cpu.cpsr.n(),
        z: cpu.cpsr.z(),
        c: cpu.cpsr.c(),
        v: cpu.cpsr.v(),
    }
}

/// ARM and Thumb are separate decoders sharing the same helpers. If they ever disagree on an
/// operation both can express, one of them is wrong — and this catches it without needing to
/// know which.
#[test]
fn arm_and_thumb_agree_on_operations_both_can_express() {
    let mut rng = Rng(0x0BAD_F00D_0BAD_F00D);
    for _ in 0..ITERATIONS {
        let (a, b) = (rng.interesting(), rng.interesting());
        let carry = rng.next_u32() & 1 != 0;

        // ADC: ARM `adcs r2, r0, r1` vs Thumb `adc r0, r1`.
        let arm = arm_op(0xE0B0_2001, a, b, carry);
        let thumb = thumb_op(0x4148, a, b, carry);
        compare("ADC ARM-vs-Thumb", a, b, carry, thumb, arm);

        // SBC: ARM `sbcs r2, r0, r1` vs Thumb `sbc r0, r1`.
        let arm = arm_op(0xE0D0_2001, a, b, carry);
        let thumb = thumb_op(0x4188, a, b, carry);
        compare("SBC ARM-vs-Thumb", a, b, carry, thumb, arm);
    }
}
