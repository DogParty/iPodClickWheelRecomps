//! ARMv4T (ARM7TDMI) interpreter core.
//!
//! Interpreter, never a JIT — iOS forbids `W^X` outside browsers, so one interpreted core is
//! the only implementation that runs on every target this project cares about (macOS, iOS, and
//! eventually a Pi). See `../../README.md#the-interpreter-decision-forced-by-ios`.
//!
//! Scope is deliberately the PP5021C's actual CPU and nothing more: ARMv4T, ARM + Thumb, no MMU,
//! no coprocessors beyond a stub. The iPod games run in system mode with no privilege separation
//! (per freemyipod), so the banked-register machinery here matters mostly for exception entry.

#![forbid(unsafe_code)]

mod arm;
mod bus;
mod cpu;
pub mod disasm;
mod thumb;

pub use bus::{Bus, FlatMemory};
pub use cpu::{Cpu, Exception, Mode, PSR};
