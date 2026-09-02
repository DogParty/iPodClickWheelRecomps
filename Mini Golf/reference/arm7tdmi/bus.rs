//! Memory interface.
//!
//! The eApp loader will implement `Bus` over the mapped game image plus MMIO stubs; `FlatMemory`
//! exists so the CPU can be tested without any of that.

/// Everything the core needs from the outside world.
///
/// Unaligned behaviour follows ARM7TDMI, and is implemented here rather than left to the
/// implementor because getting it wrong is a silent-wrong-answer bug: `LDR` from a non-word
/// address **rotates** rather than faulting, and real code relies on it.
pub trait Bus {
    fn read8(&mut self, addr: u32) -> u8;
    fn write8(&mut self, addr: u32, val: u8);

    fn read16(&mut self, addr: u32) -> u16 {
        let a = addr & !1;
        u16::from_le_bytes([self.read8(a), self.read8(a.wrapping_add(1))])
    }

    fn write16(&mut self, addr: u32, val: u16) {
        let a = addr & !1;
        let b = val.to_le_bytes();
        self.write8(a, b[0]);
        self.write8(a.wrapping_add(1), b[1]);
    }

    fn read32(&mut self, addr: u32) -> u32 {
        let a = addr & !3;
        u32::from_le_bytes([
            self.read8(a),
            self.read8(a.wrapping_add(1)),
            self.read8(a.wrapping_add(2)),
            self.read8(a.wrapping_add(3)),
        ])
    }

    fn write32(&mut self, addr: u32, val: u32) {
        let a = addr & !3;
        let b = val.to_le_bytes();
        for (i, byte) in b.iter().enumerate() {
            self.write8(a.wrapping_add(i as u32), *byte);
        }
    }

    /// `LDR` from a misaligned address reads the aligned word then rotates it right by
    /// `8 * (addr & 3)`. This is architectural on ARM7TDMI, not undefined.
    fn read32_rotated(&mut self, addr: u32) -> u32 {
        let val = self.read32(addr);
        let rot = (addr & 3) * 8;
        val.rotate_right(rot)
    }

    /// `LDRH` from an odd address likewise rotates the aligned halfword.
    fn read16_rotated(&mut self, addr: u32) -> u32 {
        let val = self.read16(addr) as u32;
        if addr & 1 != 0 {
            val.rotate_right(8)
        } else {
            val
        }
    }
}

/// A contiguous RAM region based at `base`. Reads outside it return 0 and writes are dropped —
/// tests want a predictable value, not a panic, and the real loader will supply MMIO stubs.
pub struct FlatMemory {
    pub base: u32,
    pub data: Vec<u8>,
}

impl FlatMemory {
    pub fn new(base: u32, size: usize) -> Self {
        Self {
            base,
            data: vec![0; size],
        }
    }

    /// Load `bytes` at `addr`. Panics if it would not fit — in a test that is the right
    /// behaviour, since a silently truncated fixture produces a confusing failure later.
    pub fn load(&mut self, addr: u32, bytes: &[u8]) {
        let off = (addr - self.base) as usize;
        assert!(
            off + bytes.len() <= self.data.len(),
            "load of {} bytes at {addr:#x} overruns {} byte region",
            bytes.len(),
            self.data.len()
        );
        self.data[off..off + bytes.len()].copy_from_slice(bytes);
    }

    fn index(&self, addr: u32) -> Option<usize> {
        let off = addr.wrapping_sub(self.base) as usize;
        (off < self.data.len()).then_some(off)
    }
}

impl Bus for FlatMemory {
    fn read8(&mut self, addr: u32) -> u8 {
        self.index(addr).map_or(0, |i| self.data[i])
    }

    fn write8(&mut self, addr: u32, val: u8) {
        if let Some(i) = self.index(addr) {
            self.data[i] = val;
        }
    }
}
