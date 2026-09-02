"""ARM (32-bit state) instruction decoder for the subset an armcc-built eApp uses.

The decoder turns a 32-bit word into an `Instruction` whose fields mirror the ARM Architecture
Reference Manual (ARMv4/v5 encodings, the ARM7TDMI/ARM926 the iPod runs). It deliberately covers
only what appears in game code — data processing, multiplies, single/halfword/block transfers,
branches, `bx`, `svc`, and the flag-field `mrs`/`msr` pair armcc's soft-float library uses — and
raises `UnsupportedInstruction` for anything else (coprocessor ops, `swp`, Thumb, and any
status-register transfer that would change the processor mode). An unexpected encoding should
stop the emitter loudly, not be guessed at.

Every encoding reference below is to the register-field layout in the ARM ARM, e.g. "Rn = bits
19:16". The `text` of each instruction is a human-readable mnemonic used only for comments in the
generated C++, so it is kept close to objdump's spelling without trying to match it exactly.
"""

from __future__ import annotations

from dataclasses import dataclass
from enum import Enum

CONDITIONS = ("eq", "ne", "cs", "cc", "mi", "pl", "vs", "vc", "hi", "ls", "ge", "lt", "gt", "le", "", "nv")
REGISTER_NAMES = tuple(f"r{i}" for i in range(10)) + ("sl", "fp", "ip", "sp", "lr", "pc")
SP, LR, PC = 13, 14, 15

DATA_OPS = (
    "and",
    "eor",
    "sub",
    "rsb",
    "add",
    "adc",
    "sbc",
    "rsc",
    "tst",
    "teq",
    "cmp",
    "cmn",
    "orr",
    "mov",
    "bic",
    "mvn",
)
COMPARE_OPS = frozenset({"tst", "teq", "cmp", "cmn"})  # write flags only, no destination
LOGICAL_OPS = frozenset({"and", "eor", "tst", "teq", "orr", "mov", "bic", "mvn"})  # C comes from the shifter


class UnsupportedInstruction(Exception):
    def __init__(self, address: int, word: int, why: str) -> None:
        super().__init__(f"{address:#010x}: {word:08x} — {why}")
        self.address, self.word = address, word


class ShiftType(int, Enum):
    LSL = 0
    LSR = 1
    ASR = 2
    ROR = 3  # ROR #0 encodes RRX


@dataclass(frozen=True)
class Operand2:
    """The flexible second operand of data-processing instructions."""

    immediate: int | None = None  # already rotated into place
    rotate: int = 0  # the encoded rotation, needed for the carry-out rule
    rm: int | None = None
    shift: ShiftType = ShiftType.LSL
    shift_imm: int | None = None  # immediate shift amount (LSR/ASR #0 mean #32, ROR #0 means RRX)
    shift_rs: int | None = None  # register holding the shift amount

    @property
    def is_register(self) -> bool:
        return self.rm is not None

    def text(self) -> str:
        if not self.is_register:
            return f"#{self.immediate}" if self.immediate < 10 else f"#{self.immediate:#x}"
        base = REGISTER_NAMES[self.rm]
        if self.shift_rs is not None:
            return f"{base}, {self.shift.name.lower()} {REGISTER_NAMES[self.shift_rs]}"
        if self.shift_imm == 0 and self.shift is ShiftType.LSL:
            return base
        if self.shift_imm == 0 and self.shift is ShiftType.ROR:
            return f"{base}, rrx"
        amount = self.shift_imm if self.shift_imm else 32
        return f"{base}, {self.shift.name.lower()} #{amount}"


@dataclass(frozen=True)
class Offset:
    """Addressing-mode offset for single transfers: immediate, or a register with an immediate shift."""

    immediate: int | None = None
    rm: int | None = None
    shift: ShiftType = ShiftType.LSL
    shift_imm: int = 0

    def text(self) -> str:
        if self.rm is None:
            return f"#{self.immediate}" if self.immediate < 10 else f"#{self.immediate:#x}"
        base = REGISTER_NAMES[self.rm]
        if self.shift_imm == 0 and self.shift is ShiftType.LSL:
            return base
        return f"{base}, {self.shift.name.lower()} #{self.shift_imm or 32}"


class Op(Enum):
    DATA = "data"  # and/eor/sub/.../mvn
    MUL = "mul"  # mul, mla
    MULL = "mull"  # umull, smull, umlal, smlal
    BRANCH = "b"  # b, bl
    BX = "bx"
    LOAD_STORE = "ldr"  # ldr/str, byte and word
    LOAD_STORE_HALF = "ldrh"  # ldrh/strh/ldrsb/ldrsh
    LOAD_STORE_MULTIPLE = "ldm"  # ldm/stm in all addressing modes
    MRS = "mrs"  # mrs Rd, CPSR
    MSR = "msr"  # msr CPSR_<fields>, Rm / #imm
    SVC = "svc"


@dataclass(frozen=True)
class Instruction:
    address: int
    word: int
    cond: int  # 0..14; 14 = always
    op: Op
    text: str  # disassembly for comments

    # Data processing / multiply
    name: str = ""  # "add", "cmp", "mul", "smull", ...
    sets_flags: bool = False
    rd: int = 0
    rn: int = 0
    operand2: Operand2 | None = None
    rs: int = 0  # multiplies: Rs; long multiplies: Rs; RdHi lives in rn, RdLo in rd
    rm: int = 0  # multiplies, bx

    # Branches
    link: bool = False
    target: int = 0

    # Loads and stores
    load: bool = False
    byte: bool = False  # ldrb/strb
    signed: bool = False  # ldrsb/ldrsh
    half: bool = False  # ldrh/strh/ldrsh
    pre_indexed: bool = True
    add_offset: bool = True
    writeback: bool = False
    offset: Offset | None = None
    register_list: int = 0
    user_registers: bool = False  # ldm/stm S bit

    # Status-register transfers
    psr_fields: int = 0  # msr: the four field bits, 8 = flags (`CPSR_f`)

    @property
    def is_conditional(self) -> bool:
        return self.cond != 14

    @property
    def cond_name(self) -> str:
        return CONDITIONS[self.cond]

    @property
    def next_address(self) -> int:
        return self.address + 4

    @property
    def pc_value(self) -> int:
        """What reading r15 yields while this instruction executes (pipeline: address + 8)."""
        return self.address + 8


def decode(address: int, word: int) -> Instruction:
    cond = word >> 28
    if cond == 15:
        raise UnsupportedInstruction(address, word, "NV condition (ARMv5 unconditional space)")

    if word & 0x0FFF_FFF0 == 0x012F_FF10:
        return _bx(address, word, cond)
    if word & 0x0FC0_00F0 == 0x0000_0090:
        return _multiply(address, word, cond)
    if word & 0x0F80_00F0 == 0x0080_0090:
        return _multiply_long(address, word, cond)
    if word & 0x0FB0_0FF0 == 0x0100_0090:
        raise UnsupportedInstruction(address, word, "swp")
    if word & 0x0E00_0090 == 0x0000_0090:
        return _load_store_half(address, word, cond)
    if word & 0x0FBF_0FFF == 0x010F_0000:
        return _mrs(address, word, cond)
    if word & 0x0DB0_F000 == 0x0120_F000:
        return _msr(address, word, cond)
    if word & 0x0C00_0000 == 0x0000_0000:
        return _data_processing(address, word, cond)
    if word & 0x0C00_0000 == 0x0400_0000:
        return _load_store(address, word, cond)
    if word & 0x0E00_0000 == 0x0800_0000:
        return _load_store_multiple(address, word, cond)
    if word & 0x0E00_0000 == 0x0A00_0000:
        return _branch(address, word, cond)
    if word & 0x0F00_0000 == 0x0F00_0000:
        return Instruction(
            address, word, cond, Op.SVC, f"svc{CONDITIONS[cond]} {word & 0xFFFFFF:#x}", target=word & 0xFFFFFF
        )
    raise UnsupportedInstruction(address, word, "coprocessor or undefined encoding")


def _field(word: int, high: int, low: int) -> int:
    return (word >> low) & ((1 << (high - low + 1)) - 1)


def _bit(word: int, n: int) -> bool:
    return bool(word & (1 << n))


def _ror32(value: int, amount: int) -> int:
    amount &= 31
    return ((value >> amount) | (value << (32 - amount))) & 0xFFFF_FFFF if amount else value


def _bx(address: int, word: int, cond: int) -> Instruction:
    rm = _field(word, 3, 0)
    return Instruction(address, word, cond, Op.BX, f"bx{CONDITIONS[cond]} {REGISTER_NAMES[rm]}", rm=rm)


def _mrs(address: int, word: int, cond: int) -> Instruction:
    """`mrs Rd, CPSR` — read the whole status word into a register.

    armcc's soft-float library saves and restores the condition flags around its own arithmetic
    with an `mrs`/`msr` pair; that is the only use of either instruction in this image. Reading
    SPSR is a different thing entirely (it only exists inside an exception handler) and is
    rejected, as is anything that would change the processor mode — see `_msr`.
    """
    if _bit(word, 22):
        raise UnsupportedInstruction(address, word, "mrs from SPSR (no exception handlers here)")
    rd = _field(word, 15, 12)
    return Instruction(address, word, cond, Op.MRS, f"mrs{CONDITIONS[cond]} {REGISTER_NAMES[rd]}, cpsr", rd=rd)


def _msr(address: int, word: int, cond: int) -> Instruction:
    """`msr CPSR_<fields>, Rm` or `msr CPSR_<fields>, #imm`.

    Only the flag field (`CPSR_f`, field bit 3) is accepted. The control field carries the
    processor mode and the interrupt masks, and a recompiled program has neither: a write to it
    would mean the code is doing something this translation cannot represent, so it is an error
    rather than a silent no-op.
    """
    if _bit(word, 22):
        raise UnsupportedInstruction(address, word, "msr to SPSR (no exception handlers here)")
    fields = _field(word, 19, 16)
    if fields != 0b1000:
        raise UnsupportedInstruction(address, word, f"msr to CPSR fields {fields:#06b} (only flags supported)")
    if _bit(word, 25):
        rotate = _field(word, 11, 8) * 2
        operand2 = Operand2(immediate=_ror32(_field(word, 7, 0), rotate), rotate=rotate)
    else:
        operand2 = Operand2(rm=_field(word, 3, 0), shift_imm=0)  # the register form has no shift
    return Instruction(
        address,
        word,
        cond,
        Op.MSR,
        f"msr{CONDITIONS[cond]} cpsr_f, {operand2.text()}",
        operand2=operand2,
        psr_fields=fields,
    )


def _data_processing(address: int, word: int, cond: int) -> Instruction:
    opcode = _field(word, 24, 21)
    name = DATA_OPS[opcode]
    sets_flags = _bit(word, 20)
    rn, rd = _field(word, 19, 16), _field(word, 15, 12)
    if _bit(word, 25):
        rotate = _field(word, 11, 8) * 2
        operand2 = Operand2(immediate=_ror32(_field(word, 7, 0), rotate), rotate=rotate)
    else:
        rm, shift = _field(word, 3, 0), ShiftType(_field(word, 6, 5))
        if _bit(word, 4):
            if _bit(word, 7):
                raise UnsupportedInstruction(address, word, "register shift with bit 7 set")
            operand2 = Operand2(rm=rm, shift=shift, shift_rs=_field(word, 11, 8))
        else:
            operand2 = Operand2(rm=rm, shift=shift, shift_imm=_field(word, 11, 7))

    suffix = CONDITIONS[cond]
    if name in COMPARE_OPS:
        if not sets_flags:
            raise UnsupportedInstruction(address, word, f"{name} without S bit (msr-space encoding)")
        text = f"{name}{suffix} {REGISTER_NAMES[rn]}, {operand2.text()}"
    elif name in ("mov", "mvn"):
        text = f"{name}{'s' if sets_flags else ''}{suffix} {REGISTER_NAMES[rd]}, {operand2.text()}"
    else:
        text = f"{name}{'s' if sets_flags else ''}{suffix} {REGISTER_NAMES[rd]}, {REGISTER_NAMES[rn]}, {operand2.text()}"
    return Instruction(
        address, word, cond, Op.DATA, text, name=name, sets_flags=sets_flags, rd=rd, rn=rn, operand2=operand2
    )


def _multiply(address: int, word: int, cond: int) -> Instruction:
    accumulate, sets_flags = _bit(word, 21), _bit(word, 20)
    rd, rn, rs, rm = _field(word, 19, 16), _field(word, 15, 12), _field(word, 11, 8), _field(word, 3, 0)
    name = "mla" if accumulate else "mul"
    text = f"{name}{'s' if sets_flags else ''}{CONDITIONS[cond]} {REGISTER_NAMES[rd]}, {REGISTER_NAMES[rm]}, {REGISTER_NAMES[rs]}"
    if accumulate:
        text += f", {REGISTER_NAMES[rn]}"
    return Instruction(
        address, word, cond, Op.MUL, text, name=name, sets_flags=sets_flags, rd=rd, rn=rn, rs=rs, rm=rm
    )


def _multiply_long(address: int, word: int, cond: int) -> Instruction:
    signed, accumulate, sets_flags = _bit(word, 22), _bit(word, 21), _bit(word, 20)
    rd_hi, rd_lo, rs, rm = _field(word, 19, 16), _field(word, 15, 12), _field(word, 11, 8), _field(word, 3, 0)
    name = ("s" if signed else "u") + ("mlal" if accumulate else "mull")
    text = f"{name}{'s' if sets_flags else ''}{CONDITIONS[cond]} {REGISTER_NAMES[rd_lo]}, {REGISTER_NAMES[rd_hi]}, {REGISTER_NAMES[rm]}, {REGISTER_NAMES[rs]}"
    return Instruction(
        address,
        word,
        cond,
        Op.MULL,
        text,
        name=name,
        sets_flags=sets_flags,
        signed=signed,
        rd=rd_lo,
        rn=rd_hi,
        rs=rs,
        rm=rm,
    )


def _addressing_text(insn_rn: int, offset: Offset, pre: bool, add: bool, writeback: bool) -> str:
    base = REGISTER_NAMES[insn_rn]
    if offset.rm is None and offset.immediate == 0:
        return f"[{base}]"
    sign = "" if add else "-"
    text = offset.text()
    signed_offset = f"#{sign}{text[1:]}" if text.startswith("#") else f"{sign}{text}"
    if pre:
        return f"[{base}, {signed_offset}]" + ("!" if writeback else "")
    return f"[{base}], {signed_offset}"


def _load_store(address: int, word: int, cond: int) -> Instruction:
    pre, add, byte, writeback, load = (_bit(word, n) for n in (24, 23, 22, 21, 20))
    rn, rd = _field(word, 19, 16), _field(word, 15, 12)
    if _bit(word, 25):
        if _bit(word, 4):
            raise UnsupportedInstruction(address, word, "undefined single-transfer encoding")
        offset = Offset(
            rm=_field(word, 3, 0), shift=ShiftType(_field(word, 6, 5)), shift_imm=_field(word, 11, 7)
        )
    else:
        offset = Offset(immediate=_field(word, 11, 0))
    if not pre and writeback:
        raise UnsupportedInstruction(address, word, "ldrt/strt (user-mode translation)")
    name = ("ldr" if load else "str") + ("b" if byte else "") + CONDITIONS[cond]
    text = f"{name} {REGISTER_NAMES[rd]}, {_addressing_text(rn, offset, pre, add, writeback)}"
    return Instruction(
        address,
        word,
        cond,
        Op.LOAD_STORE,
        text,
        load=load,
        byte=byte,
        rd=rd,
        rn=rn,
        pre_indexed=pre,
        add_offset=add,
        writeback=writeback,
        offset=offset,
    )


def _load_store_half(address: int, word: int, cond: int) -> Instruction:
    pre, add, immediate, writeback, load = (_bit(word, n) for n in (24, 23, 22, 21, 20))
    rn, rd, kind = _field(word, 19, 16), _field(word, 15, 12), _field(word, 6, 5)
    if immediate:
        offset = Offset(immediate=(_field(word, 11, 8) << 4) | _field(word, 3, 0))
    else:
        offset = Offset(rm=_field(word, 3, 0))
    if not load and kind != 1:
        raise UnsupportedInstruction(address, word, "ldrd/strd (ARMv5TE doubleword transfer)")
    signed, half = kind in (2, 3), kind in (1, 3)
    name = ("ldr" if load else "str") + ("s" if signed else "") + ("h" if half else "b") + CONDITIONS[cond]
    text = f"{name} {REGISTER_NAMES[rd]}, {_addressing_text(rn, offset, pre, add, writeback)}"
    return Instruction(
        address,
        word,
        cond,
        Op.LOAD_STORE_HALF,
        text,
        load=load,
        signed=signed,
        half=half,
        rd=rd,
        rn=rn,
        pre_indexed=pre,
        add_offset=add,
        writeback=writeback,
        offset=offset,
    )


def _load_store_multiple(address: int, word: int, cond: int) -> Instruction:
    pre, add, user, writeback, load = (_bit(word, n) for n in (24, 23, 22, 21, 20))
    rn, register_list = _field(word, 19, 16), _field(word, 15, 0)
    if user:
        raise UnsupportedInstruction(address, word, "ldm/stm with S bit (user registers / SPSR restore)")
    regs = ", ".join(REGISTER_NAMES[i] for i in range(16) if register_list & (1 << i))
    suffix = CONDITIONS[cond]
    if rn == SP and writeback and load and add and not pre:
        text = f"pop{suffix} {{{regs}}}"
    elif rn == SP and writeback and not load and not add and pre:
        text = f"push{suffix} {{{regs}}}"
    else:
        mode = {(True, True): "ib", (False, True): "ia", (True, False): "db", (False, False): "da"}[
            (pre, add)
        ]
        text = f"{'ldm' if load else 'stm'}{mode}{suffix} {REGISTER_NAMES[rn]}{'!' if writeback else ''}, {{{regs}}}"
    return Instruction(
        address,
        word,
        cond,
        Op.LOAD_STORE_MULTIPLE,
        text,
        load=load,
        rn=rn,
        register_list=register_list,
        pre_indexed=pre,
        add_offset=add,
        writeback=writeback,
    )


def _branch(address: int, word: int, cond: int) -> Instruction:
    link = _bit(word, 24)
    offset = _field(word, 23, 0)
    if offset & 0x80_0000:
        offset -= 0x100_0000
    target = (address + 8 + offset * 4) & 0xFFFF_FFFF
    name = ("bl" if link else "b") + CONDITIONS[cond]
    text = f"{name} {target:#x}" if target != address else "b ."
    return Instruction(address, word, cond, Op.BRANCH, text, link=link, target=target)
