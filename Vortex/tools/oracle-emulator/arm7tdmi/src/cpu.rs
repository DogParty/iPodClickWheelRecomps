//! Core state: registers, banking, program status, exception entry.

use crate::bus::Bus;
use crate::{arm, thumb};

/// Processor mode, as encoded in `CPSR[4:0]`.
#[derive(Copy, Clone, PartialEq, Eq, Debug)]
pub enum Mode {
    User = 0b10000,
    Fiq = 0b10001,
    Irq = 0b10010,
    Supervisor = 0b10011,
    Abort = 0b10111,
    Undefined = 0b11011,
    System = 0b11111,
}

impl Mode {
    pub fn from_bits(bits: u32) -> Option<Mode> {
        Some(match bits & 0x1F {
            0b10000 => Mode::User,
            0b10001 => Mode::Fiq,
            0b10010 => Mode::Irq,
            0b10011 => Mode::Supervisor,
            0b10111 => Mode::Abort,
            0b11011 => Mode::Undefined,
            0b11111 => Mode::System,
            _ => return None,
        })
    }

    /// User and System share one register bank and have no SPSR.
    fn has_spsr(self) -> bool {
        !matches!(self, Mode::User | Mode::System)
    }
}

/// Program status register.
#[derive(Copy, Clone, PartialEq, Eq, Default, Debug)]
pub struct PSR(pub u32);

macro_rules! psr_flag {
    ($get:ident, $set:ident, $bit:expr) => {
        #[inline]
        pub fn $get(self) -> bool {
            self.0 & (1 << $bit) != 0
        }
        #[inline]
        pub fn $set(&mut self, v: bool) {
            self.0 = (self.0 & !(1 << $bit)) | ((v as u32) << $bit);
        }
    };
}

impl PSR {
    psr_flag!(n, set_n, 31);
    psr_flag!(z, set_z, 30);
    psr_flag!(c, set_c, 29);
    psr_flag!(v, set_v, 28);
    psr_flag!(irq_disabled, set_irq_disabled, 7);
    psr_flag!(fiq_disabled, set_fiq_disabled, 6);
    psr_flag!(thumb, set_thumb, 5);

    pub fn mode(self) -> Mode {
        Mode::from_bits(self.0).unwrap_or(Mode::System)
    }

    pub fn set_mode(&mut self, m: Mode) {
        self.0 = (self.0 & !0x1F) | m as u32;
    }
}

/// Exceptions, with their vector addresses.
#[derive(Copy, Clone, PartialEq, Eq, Debug)]
pub enum Exception {
    Reset,
    Undefined,
    SoftwareInterrupt,
    PrefetchAbort,
    DataAbort,
    Irq,
    Fiq,
}

impl Exception {
    fn vector(self) -> u32 {
        match self {
            Exception::Reset => 0x00,
            Exception::Undefined => 0x04,
            Exception::SoftwareInterrupt => 0x08,
            Exception::PrefetchAbort => 0x0C,
            Exception::DataAbort => 0x10,
            Exception::Irq => 0x18,
            Exception::Fiq => 0x1C,
        }
    }

    fn target_mode(self) -> Mode {
        match self {
            Exception::Reset | Exception::SoftwareInterrupt => Mode::Supervisor,
            Exception::Undefined => Mode::Undefined,
            Exception::PrefetchAbort | Exception::DataAbort => Mode::Abort,
            Exception::Irq => Mode::Irq,
            Exception::Fiq => Mode::Fiq,
        }
    }
}

pub struct Cpu {
    /// The live register view. `regs[15]` is the address of the *next instruction to fetch* —
    /// not the architectural `PC` value, which is ahead by one pipeline refill. Use
    /// [`Cpu::read_reg`] rather than indexing this directly, or `R15` reads will be wrong.
    pub regs: [u32; 16],
    pub cpsr: PSR,
    /// `--null-dispatch=survive` — treat `BX` to address 0 as "the call returned null" rather than
    /// branching there.
    ///
    /// A DIAGNOSTIC, never a fix. RetailOS invokes an unbound delegate, loads a zero vtable slot and
    /// branches to 0, which post-MMAP is its own reset vector. That is a real fault and modelling it
    /// away would be lying. But it is also a wall, and the only way to see what is behind a wall is
    /// to step over it once: with this set, the dispatch returns 0 to the caller — which is exactly
    /// what the caller's own error path is written to handle — and the boot continues.
    pub survive_null_dispatch: bool,
    pub null_dispatches: u64,

    // Banked r8..r14. User and System share `bank_usr`.
    bank_usr: [u32; 7],
    bank_fiq: [u32; 7],
    bank_svc: [u32; 2],
    bank_abt: [u32; 2],
    bank_irq: [u32; 2],
    bank_und: [u32; 2],

    spsr_fiq: PSR,
    spsr_svc: PSR,
    spsr_abt: PSR,
    spsr_irq: PSR,
    spsr_und: PSR,

    /// Set when an instruction wrote R15, so `step` knows not to advance sequentially.
    pub(crate) branched: bool,
}

impl Default for Cpu {
    fn default() -> Self {
        Self::new()
    }
}

impl Cpu {
    pub fn new() -> Self {
        let mut cpsr = PSR::default();
        cpsr.set_mode(Mode::Supervisor);
        cpsr.set_irq_disabled(true);
        cpsr.set_fiq_disabled(true);
        Self {
            regs: [0; 16],
            cpsr,
            survive_null_dispatch: false,
            null_dispatches: 0,
            bank_usr: [0; 7],
            bank_fiq: [0; 7],
            bank_svc: [0; 2],
            bank_abt: [0; 2],
            bank_irq: [0; 2],
            bank_und: [0; 2],
            spsr_fiq: PSR::default(),
            spsr_svc: PSR::default(),
            spsr_abt: PSR::default(),
            spsr_irq: PSR::default(),
            spsr_und: PSR::default(),
            branched: false,
        }
    }

    // ---------------------------------------------------------------- registers

    /// Read a register with correct `R15` pipeline semantics: ARM sees `addr + 8`,
    /// Thumb sees `addr + 4`.
    #[inline]
    pub fn read_reg(&self, r: usize) -> u32 {
        if r == 15 {
            let ahead = if self.cpsr.thumb() { 2 } else { 4 };
            self.regs[15].wrapping_add(ahead)
        } else {
            self.regs[r]
        }
    }

    /// `R15` as seen by a data-processing operand that uses a *register-specified* shift —
    /// one word further ahead again (`addr + 12`). This extra displacement is the classic
    /// ARM7 quirk that silently corrupts results if you skip it.
    #[inline]
    pub fn read_reg_shifted(&self, r: usize) -> u32 {
        if r == 15 {
            self.regs[15].wrapping_add(8)
        } else {
            self.regs[r]
        }
    }

    /// Write a register. Writing `R15` branches, aligning to the current instruction width.
    #[inline]
    pub fn write_reg(&mut self, r: usize, val: u32) {
        if r == 15 {
            let mask = if self.cpsr.thumb() { !1 } else { !3 };
            self.regs[15] = val & mask;
            self.branched = true;
        } else {
            self.regs[r] = val;
        }
    }

    /// `BX`-style write: bit 0 selects Thumb.
    pub fn branch_exchange(&mut self, val: u32) {
        self.cpsr.set_thumb(val & 1 != 0);
        let mask = if self.cpsr.thumb() { !1 } else { !3 };
        self.regs[15] = val & mask;
        self.branched = true;
    }

    // ---------------------------------------------------------------- mode / SPSR

    pub fn spsr(&self) -> PSR {
        match self.cpsr.mode() {
            Mode::Fiq => self.spsr_fiq,
            Mode::Supervisor => self.spsr_svc,
            Mode::Abort => self.spsr_abt,
            Mode::Irq => self.spsr_irq,
            Mode::Undefined => self.spsr_und,
            // User/System have no SPSR; reading returns CPSR rather than faulting.
            _ => self.cpsr,
        }
    }

    pub fn set_spsr(&mut self, v: PSR) {
        match self.cpsr.mode() {
            Mode::Fiq => self.spsr_fiq = v,
            Mode::Supervisor => self.spsr_svc = v,
            Mode::Abort => self.spsr_abt = v,
            Mode::Irq => self.spsr_irq = v,
            Mode::Undefined => self.spsr_und = v,
            _ => {}
        }
    }

    /// Swap the banked registers when changing mode. Must be called *before* `cpsr.set_mode`,
    /// with the mode being left and the mode being entered.
    pub fn switch_mode(&mut self, from: Mode, to: Mode) {
        if from == to {
            return;
        }
        // `bank_usr[0..5]` holds r8–r12, which every non-FIQ mode shares. `bank_usr[5..7]`
        // holds r13/r14 for User and System *only* — the other privileged modes keep theirs in
        // their own two-word bank. Writing r13/r14 into `bank_usr` from an IRQ/SVC/ABT/UND
        // excursion would clobber the User/System stack pointer on the way back out.
        match from {
            Mode::Fiq => self.bank_fiq.copy_from_slice(&self.regs[8..15]),
            _ => {
                self.bank_usr[0..5].copy_from_slice(&self.regs[8..13]);
                match from {
                    Mode::User | Mode::System => {
                        self.bank_usr[5..7].copy_from_slice(&self.regs[13..15])
                    }
                    Mode::Supervisor => self.bank_svc.copy_from_slice(&self.regs[13..15]),
                    Mode::Abort => self.bank_abt.copy_from_slice(&self.regs[13..15]),
                    Mode::Irq => self.bank_irq.copy_from_slice(&self.regs[13..15]),
                    Mode::Undefined => self.bank_und.copy_from_slice(&self.regs[13..15]),
                    Mode::Fiq => unreachable!("handled above"),
                }
            }
        }
        match to {
            Mode::Fiq => self.regs[8..15].copy_from_slice(&self.bank_fiq),
            _ => {
                self.regs[8..13].copy_from_slice(&self.bank_usr[0..5]);
                match to {
                    Mode::User | Mode::System => {
                        self.regs[13..15].copy_from_slice(&self.bank_usr[5..7])
                    }
                    Mode::Supervisor => self.regs[13..15].copy_from_slice(&self.bank_svc),
                    Mode::Abort => self.regs[13..15].copy_from_slice(&self.bank_abt),
                    Mode::Irq => self.regs[13..15].copy_from_slice(&self.bank_irq),
                    Mode::Undefined => self.regs[13..15].copy_from_slice(&self.bank_und),
                    Mode::Fiq => unreachable!("handled above"),
                }
            }
        }
    }

    pub fn set_mode(&mut self, to: Mode) {
        let from = self.cpsr.mode();
        self.switch_mode(from, to);
        self.cpsr.set_mode(to);
    }

    /// Restore `CPSR` from the current mode's `SPSR` — the `S`-bit-with-R15 return path used by
    /// `MOVS pc, lr` and `LDM ^`.
    pub fn restore_cpsr(&mut self) {
        if !self.cpsr.mode().has_spsr() {
            return;
        }
        let saved = self.spsr();
        let to = saved.mode();
        self.switch_mode(self.cpsr.mode(), to);
        self.cpsr = saved;
    }

    // ---------------------------------------------------------------- exceptions

    pub fn raise(&mut self, e: Exception) {
        let saved = self.cpsr;
        // Return address, relative to the instruction that faulted. `regs[15]` currently points
        // one instruction past it, so the ARM-state offsets collapse to these.
        let width: u32 = if saved.thumb() { 2 } else { 4 };
        let lr = match e {
            Exception::Irq | Exception::Fiq | Exception::DataAbort => {
                self.regs[15].wrapping_add(width)
            }
            _ => self.regs[15],
        };

        let to = e.target_mode();
        self.set_mode(to);
        self.set_spsr(saved);
        self.regs[14] = lr;
        self.cpsr.set_thumb(false);
        self.cpsr.set_irq_disabled(true);
        if matches!(e, Exception::Reset | Exception::Fiq) {
            self.cpsr.set_fiq_disabled(true);
        }
        self.regs[15] = e.vector();
        self.branched = true;
    }

    /// Deliver an IRQ if not masked. Returns whether it was taken.
    pub fn irq(&mut self) -> bool {
        if self.cpsr.irq_disabled() {
            return false;
        }
        self.raise(Exception::Irq);
        true
    }

    // ---------------------------------------------------------------- execution

    /// Fetch, decode and execute one instruction.
    pub fn step(&mut self, bus: &mut impl Bus) {
        self.branched = false;
        if self.cpsr.thumb() {
            let addr = self.regs[15] & !1;
            let instr = bus.read16(addr);
            self.regs[15] = addr.wrapping_add(2);
            thumb::execute(self, bus, instr);
        } else {
            let addr = self.regs[15] & !3;
            let instr = bus.read32(addr);
            self.regs[15] = addr.wrapping_add(4);
            arm::execute(self, bus, instr);
        }
    }

    /// Run `n` instructions.
    pub fn run(&mut self, bus: &mut impl Bus, n: usize) {
        for _ in 0..n {
            self.step(bus);
        }
    }

    // ---------------------------------------------------------------- flag helpers

    #[inline]
    pub(crate) fn set_nz(&mut self, result: u32) {
        self.cpsr.set_n(result & 0x8000_0000 != 0);
        self.cpsr.set_z(result == 0);
    }
}

/// Condition field evaluation. `0b1111` (NV) is architecturally *never* on ARMv4T.
pub(crate) fn cond_passes(cond: u32, psr: PSR) -> bool {
    let (n, z, c, v) = (psr.n(), psr.z(), psr.c(), psr.v());
    match cond {
        0x0 => z,
        0x1 => !z,
        0x2 => c,
        0x3 => !c,
        0x4 => n,
        0x5 => !n,
        0x6 => v,
        0x7 => !v,
        0x8 => c && !z,
        0x9 => !c || z,
        0xA => n == v,
        0xB => n != v,
        0xC => !z && (n == v),
        0xD => z || (n != v),
        0xE => true,
        _ => false,
    }
}

// ---------------------------------------------------------------- state capture

/// Number of `u32` words in a [`Cpu::save`] image.
pub const CPU_STATE_WORDS: usize = 16 + 1 + 7 + 7 + 2 + 2 + 2 + 2 + 5 + 1;

impl Cpu {
    /// Flatten the whole architectural state, banked registers included.
    ///
    /// This exists so a run can be snapshotted and resumed. The banked registers are private, and
    /// deliberately stay that way — serialising from inside the type keeps the invariant that
    /// nothing outside can desynchronise `regs` from the bank it was switched out of.
    pub fn save(&self) -> Vec<u32> {
        let mut v = Vec::with_capacity(CPU_STATE_WORDS);
        v.extend_from_slice(&self.regs);
        v.push(self.cpsr.0);
        v.extend_from_slice(&self.bank_usr);
        v.extend_from_slice(&self.bank_fiq);
        v.extend_from_slice(&self.bank_svc);
        v.extend_from_slice(&self.bank_abt);
        v.extend_from_slice(&self.bank_irq);
        v.extend_from_slice(&self.bank_und);
        v.push(self.spsr_fiq.0);
        v.push(self.spsr_svc.0);
        v.push(self.spsr_abt.0);
        v.push(self.spsr_irq.0);
        v.push(self.spsr_und.0);
        v.push(self.branched as u32);
        debug_assert_eq!(v.len(), CPU_STATE_WORDS);
        v
    }

    /// Inverse of [`Cpu::save`]. Returns false if the image is the wrong length.
    pub fn load(&mut self, v: &[u32]) -> bool {
        if v.len() != CPU_STATE_WORDS {
            return false;
        }
        let mut i = 0;
        let mut take = |n: usize| {
            let s = &v[i..i + n];
            i += n;
            s
        };
        self.regs.copy_from_slice(take(16));
        self.cpsr = PSR(take(1)[0]);
        self.bank_usr.copy_from_slice(take(7));
        self.bank_fiq.copy_from_slice(take(7));
        self.bank_svc.copy_from_slice(take(2));
        self.bank_abt.copy_from_slice(take(2));
        self.bank_irq.copy_from_slice(take(2));
        self.bank_und.copy_from_slice(take(2));
        self.spsr_fiq = PSR(take(1)[0]);
        self.spsr_svc = PSR(take(1)[0]);
        self.spsr_abt = PSR(take(1)[0]);
        self.spsr_irq = PSR(take(1)[0]);
        self.spsr_und = PSR(take(1)[0]);
        self.branched = take(1)[0] != 0;
        true
    }
}
