"""Control flow: which instructions belong to a function and what each branch means.

Starting at a function's entry, `discover` follows every path the instruction stream can take —
fall-through, local branches, jump tables — and stops at returns, unconditional tail calls, and
assert traps. The result is the exact set of instructions the generated C++ must contain, with a
`Role` attached to every control-flow instruction telling the emitter what to write for it.

Why walk the control flow instead of emitting Ghidra's address range: armcc interleaves literal
pools with code and sometimes lets two functions share a tail. Walking from the entry never
emits data as code, and a shared tail is simply emitted in both functions.

The idioms recognised here are the ones the binary actually uses (counted in the analysis):

* `bl target`                      call; `target` is a game function, a runtime entry, or a thunk
* `b target` to another entry      tail call (armcc emits these constantly)
* `b .`                            assert trap — an infinite loop the original used as `abort()`
* `bx lr`, `mov pc, lr`,
  `ldm/pop {..., pc}`               return
* `mov lr, pc` + `mov pc, rN`/`bx rN`   indirect call through a register (vtables, callbacks);
  also `add lr, pc, #4` a few instructions earlier, in the static-initialiser runner
* `cmp rN, #K` + `addls pc, pc, rN, lsl #2`   switch: case i jumps to site + 8 + 4*i, i in 0..K
  (`lsl #3` — two instructions per case — is the same table with a stride of 8)
* `and rN, rM, #MASK` ... `add pc, pc, rN, lsl #2`   the same switch, unconditional, bounded by the
  mask that computed the index rather than by a compare — armcc's unrolled 64-bit divide steps
  into its loop body this way (the index is `3 * (7 - (count & 7))`)
"""

from __future__ import annotations

from dataclasses import dataclass, field
from enum import Enum

from .arm import LR, PC, SP, Instruction, Op, ShiftType, decode
from .image import EAppImage


class Role(Enum):
    NORMAL = "normal"
    LOCAL_BRANCH = "local_branch"
    CALL = "call"
    TAIL_CALL = "tail_call"
    RETURN = "return"
    INDIRECT_CALL = "indirect_call"  # mov lr, pc; mov pc, rN  (the mov pc / bx carries the role)
    INDIRECT_JUMP = "indirect_jump"  # bx rN with no link: tail call through a register
    JUMP_TABLE = "jump_table"
    TRAP = "trap"


@dataclass(frozen=True)
class JumpTable:
    site: int
    index_register: int
    case_count: int
    # Bytes between one case's code and the next: 4 for `lsl #2` (one instruction per case, a
    # branch), 8 for `lsl #3` (two per case — armcc's unrolled loops step into a body whose
    # cases are pairs of instructions), and so on.
    stride: int = 4

    def case_target(self, index: int) -> int:
        return self.site + 8 + self.stride * index


@dataclass
class FunctionBody:
    entry: int
    instructions: dict[int, Instruction] = field(default_factory=dict)  # address -> instruction, unordered
    roles: dict[int, Role] = field(default_factory=dict)
    labels: set[int] = field(default_factory=set)  # addresses that need a C++ label
    jump_tables: dict[int, JumpTable] = field(default_factory=dict)
    call_targets: set[int] = field(default_factory=set)  # every bl / tail-call destination

    def ordered(self) -> list[Instruction]:
        return [self.instructions[a] for a in sorted(self.instructions)]


class ControlFlowError(Exception):
    pass


# How far back to look for the `cmp` that bounds a jump table. armcc usually places it within a
# couple of instructions, occasionally eight; the search stops at the first flag-setting
# instruction either way, so a wide window is safe.
JUMP_TABLE_COMPARE_WINDOW = 16

# How far before a register jump the `mov lr, pc` / `add lr, pc, #N` that makes it a call can be.
#
# Three titles have moved this. Texas Hold'em's compiler put the setup four instructions before
# its `bx` (`add lr, pc, #0xc` at 0x1800b8e8), The Sims Bowling's twelve (`add lr, pc, #0x2c` at
# 0x18044a84) and then *twenty-four* (`add lr, pc, #0x5c` at 0x18028f68, followed by the whole
# of a 16.16 multiply chain before the `bx r2` at 0x18028fc8). Every time the window was one
# short the jump read as a tail call, the code after it — that path's epilogue — was never
# emitted, and the function returned a frame early with its locals still on the stack; the
# fault surfaced several calls later as a return address popped from somebody's local variable.
#
# So the scan is no longer a window in any practical sense: it runs back until something else
# writes lr — a `bl`, a pop that restores it, a load or a move into it — because past that point
# the prepared value is gone whatever it was. What makes the scan safe at any length is the
# equality test in `_follows_link_setup`: only a prepared return address that is exactly the
# address after the jump counts. The bound below is a backstop, not a tuning parameter.
LINK_SETUP_WINDOW = 256


def discover(entry: int, image: EAppImage, known_entries: set[int]) -> FunctionBody:
    """Collect the body of the function at `entry`.

    `known_entries` decides whether an unconditional `b` is a local branch or a tail call to
    another function. Targets of `bl` that are not yet known are still recorded in
    `call_targets`; the caller adds them to the table and emits them too.
    """
    body = FunctionBody(entry)
    worklist = [entry]
    while worklist:
        address = worklist.pop()
        if address in body.instructions:
            continue
        if not image.contains(address):
            raise ControlFlowError(f"{entry:#010x}: control flow leaves the image at {address:#010x}")
        insn = decode(address, image.u32(address))
        body.instructions[address] = insn
        role, successors = _classify(insn, body, image, known_entries)
        body.roles[address] = role
        if role is Role.CALL or role is Role.TAIL_CALL:
            body.call_targets.add(insn.target)
        previous = body.instructions.get(address - 4)
        if previous is not None and _complementary_terminals(previous, insn, body.roles[address - 4], role):
            successors = [s for s in successors if s != insn.next_address]
        worklist.extend(successors)
    return body


def _classify(
    insn: Instruction, body: FunctionBody, image: EAppImage, known_entries: set[int]
) -> tuple[Role, list[int]]:
    """Return the instruction's role and the addresses control can flow to next."""
    fall_through = [insn.next_address]

    if insn.op is Op.BRANCH:
        if insn.link:
            return Role.CALL, fall_through
        if insn.target == insn.address:
            return Role.TRAP, _if_conditional(insn, fall_through)
        if insn.target in known_entries and insn.target != body.entry:
            return Role.TAIL_CALL, _if_conditional(insn, fall_through)
        body.labels.add(insn.target)
        return Role.LOCAL_BRANCH, [insn.target] + _if_conditional(insn, fall_through)

    if insn.op is Op.BX:
        if insn.rm == LR:
            return Role.RETURN, _if_conditional(insn, fall_through)
        if _follows_link_setup(insn, body):
            return Role.INDIRECT_CALL, fall_through
        return Role.INDIRECT_JUMP, _if_conditional(insn, fall_through)

    if insn.op is Op.DATA and insn.rd == PC and insn.name not in ("cmp", "cmn", "tst", "teq"):
        return _classify_pc_write(insn, body, image)

    if insn.op is Op.LOAD_STORE and insn.load and insn.rd == PC:
        if insn.rn == SP and not insn.pre_indexed:
            return Role.RETURN, _if_conditional(insn, fall_through)  # ldr pc, [sp], #4
        raise ControlFlowError(f"{insn.address:#010x}: unsupported load into pc: {insn.text}")

    if insn.op is Op.LOAD_STORE_MULTIPLE and insn.load and insn.register_list & (1 << PC):
        return Role.RETURN, _if_conditional(insn, fall_through)

    return Role.NORMAL, fall_through


def _classify_pc_write(insn: Instruction, body: FunctionBody, image: EAppImage) -> tuple[Role, list[int]]:
    fall_through = [insn.next_address]
    op2 = insn.operand2
    if insn.name == "mov" and op2.is_register and op2.shift_imm == 0 and op2.shift is ShiftType.LSL:
        if op2.rm == LR:
            return Role.RETURN, _if_conditional(insn, fall_through)
        if _follows_link_setup(insn, body):
            return Role.INDIRECT_CALL, fall_through
        return Role.INDIRECT_JUMP, _if_conditional(insn, fall_through)

    if (
        insn.name == "add"
        and insn.rn == PC
        and op2.is_register
        and op2.shift is ShiftType.LSL
        and op2.shift_rs is None
        and op2.shift_imm is not None
        and op2.shift_imm >= 2
    ):
        table = _resolve_jump_table(insn, body, image, stride=1 << op2.shift_imm)
        body.jump_tables[insn.address] = table
        targets = [table.case_target(i) for i in range(table.case_count)]
        body.labels.update(targets)
        return Role.JUMP_TABLE, targets + fall_through

    raise ControlFlowError(f"{insn.address:#010x}: unsupported write to pc: {insn.text}")


def _follows_link_setup(insn: Instruction, body: FunctionBody) -> bool:
    """True when an earlier instruction set lr to this instruction's return address.

    armcc writes an indirect call as `mov lr, pc` directly before `mov pc, rN`/`bx rN`, or —
    in its static-initialiser runner — as `add lr, pc, #4` a couple of instructions before
    `mov pc, r0`. Either way lr ends up equal to the address after the jump, and that is the
    test: a register jump whose return address was prepared is a call, not a tail call.
    """
    for back in range(1, LINK_SETUP_WINDOW + 1):
        previous = body.instructions.get(insn.address - 4 * back)
        if previous is None:
            continue
        if not _writes_register(previous, LR):
            continue
        # The nearest thing that wrote lr decides. A `bl`, a pop, a load, a move from another
        # register: lr no longer holds a prepared return address, and nothing earlier can be
        # this jump's.
        if previous.op is not Op.DATA:
            return False
        op2 = previous.operand2
        if previous.name == "mov" and op2.is_register and op2.rm == PC and op2.shift_imm == 0:
            link_value = previous.pc_value
        elif previous.name == "add" and previous.rn == PC and not op2.is_register:
            link_value = previous.pc_value + op2.immediate
        else:
            return False
        return link_value == insn.next_address
    return False


def _resolve_jump_table(
    insn: Instruction, body: FunctionBody, image: EAppImage, stride: int = 4
) -> JumpTable:
    """Find what bounds the index of `add pc, pc, rN, lsl #2` and derive the case count.

    Two shapes. The usual one is a guarded table — `cmp rN, #K` then `addls` (or `addcc`) — where
    the compare is the bound. The other is an *unconditional* jump whose index was just computed
    from a mask, which armcc uses to step into an unrolled loop; there is no compare, and the
    bound is whatever the mask and the arithmetic after it allow (`_mask_bound`).
    """
    index_register = insn.operand2.rm
    if insn.cond_name in ("ls", "cc"):
        for back in range(1, JUMP_TABLE_COMPARE_WINDOW + 1):
            address = insn.address - 4 * back
            guard = body.instructions.get(address) or decode(address, image.u32(address))
            if guard.op is Op.DATA and guard.sets_flags:
                if guard.name == "cmp" and guard.rn == index_register and not guard.operand2.is_register:
                    limit = guard.operand2.immediate
                    case_count = limit + 1 if insn.cond_name == "ls" else limit
                    return JumpTable(insn.address, index_register, case_count, stride)
                break
        raise ControlFlowError(f"{insn.address:#010x}: jump table without a recognisable cmp guard")
    if not insn.is_conditional:
        bound = _mask_bound(insn, index_register, body, image)
        if bound is not None:
            return JumpTable(insn.address, index_register, bound + 1, stride)
        raise ControlFlowError(f"{insn.address:#010x}: unconditional jump table without a recognisable mask")
    raise ControlFlowError(f"{insn.address:#010x}: jump table with condition '{insn.cond_name}'")


def _mask_bound(insn: Instruction, index_register: int, body: FunctionBody, image: EAppImage) -> int | None:
    """The largest value the index register can hold at an unconditional computed jump, or None.

    Walks back to the most recent `and rN, rM, #MASK` that wrote the index register, then forward
    again through the instructions between it and the jump, tracking the largest value the
    register can have: an `and` with an immediate caps it at that immediate; an `eor` with an
    immediate no larger than a cap that is all ones keeps it; `rsb rN, rN, #K` with K at least the
    cap makes K the cap; `add rN, rN, rN, lsl #k`, `add rN, rN, #k` and `mov rN, rN, lsl #k`
    scale or shift it. Anything else that writes the
    register — a load, a shift by a register, an arithmetic shape not listed — means the bound
    is unknown, and the caller reports the jump as unsupported rather than guessing: a table
    with too few cases would trap on a real input, and that is the failure this must not have.

    What this does not check is that every path to the jump goes through the mask. The idiom
    it exists for is a straight run of data-processing instructions, and a branch *into* the
    middle of that run would need a label there; the window is short for that reason.
    """
    chain: list[Instruction] = []
    for back in range(1, JUMP_TABLE_COMPARE_WINDOW + 1):
        address = insn.address - 4 * back
        previous = body.instructions.get(address) or decode(address, image.u32(address))
        chain.append(previous)
        if (
            previous.op is Op.DATA
            and previous.name == "and"
            and previous.rd == index_register
            and not previous.operand2.is_register
            and not previous.is_conditional
        ):
            break  # the mask that starts the chain; what follows it is checked forwards
    else:
        return None
    chain.reverse()  # now in execution order, the mask first
    bound = chain[0].operand2.immediate
    for step in chain[1:]:
        if not _writes_register(step, index_register):
            if step.op is not Op.DATA:
                return None  # a load or branch in the run: not the straight-line idiom
            continue
        if step.op is not Op.DATA or step.is_conditional:
            return None
        op2 = step.operand2
        if step.name == "eor" and not op2.is_register and step.rn == index_register:
            all_ones = (bound & (bound + 1)) == 0
            if not (all_ones and op2.immediate <= bound):
                return None
        elif step.name == "add" and step.rn == index_register and not op2.is_register:
            bound += op2.immediate
        elif step.name == "rsb" and step.rn == index_register and not op2.is_register:
            if op2.immediate < bound:
                return None  # `K - index` would go negative for some index
            bound = op2.immediate  # `K - index` for index in 0..bound is at most K
        elif (
            step.name == "add"
            and step.rn == index_register
            and op2.is_register
            and op2.rm == index_register
            and op2.shift is ShiftType.LSL
            and op2.shift_rs is None
        ):
            bound += bound << op2.shift_imm
        elif (
            step.name == "mov"
            and op2.is_register
            and op2.rm == index_register
            and op2.shift is ShiftType.LSL
            and op2.shift_rs is None
        ):
            bound <<= op2.shift_imm
        else:
            return None
    return bound


def _writes_register(insn: Instruction, register: int) -> bool:
    """Whether the instruction leaves a new value in `register`."""
    if insn.op is Op.BRANCH:
        return insn.link and register == LR
    if insn.op is Op.DATA:
        return insn.name not in ("cmp", "cmn", "tst", "teq") and insn.rd == register
    if insn.op in (Op.MUL, Op.MULL, Op.MRS):
        return insn.rd == register or (insn.op is Op.MULL and insn.rn == register)
    if insn.op in (Op.LOAD_STORE, Op.LOAD_STORE_HALF):
        return (insn.load and insn.rd == register) or (insn.writeback and insn.rn == register)
    if insn.op is Op.LOAD_STORE_MULTIPLE:
        loaded = insn.load and (insn.register_list >> register) & 1 != 0
        return loaded or (insn.writeback and insn.rn == register)
    return False


def _if_conditional(insn: Instruction, successors: list[int]) -> list[int]:
    """A conditional terminal instruction still falls through when its condition fails."""
    return successors if insn.is_conditional else []


COMPLEMENTARY = {0: 1, 1: 0, 2: 3, 3: 2, 4: 5, 5: 4, 6: 7, 7: 6, 8: 9, 9: 8, 10: 11, 11: 10, 12: 13, 13: 12}


def _complementary_terminals(
    first: Instruction, second: Instruction, first_role: Role, second_role: Role
) -> bool:
    """`bne X` directly followed by `beq Y` (or popne/popeq, bxne/bxeq ...) never falls through.

    armcc emits the pair for an if/else whose both arms end in a jump or return. Nothing can
    reach the instruction after the pair, which in practice is the literal pool, so treating the
    pair as terminal keeps data from being decoded as code.
    """
    terminal = {Role.RETURN, Role.TAIL_CALL, Role.LOCAL_BRANCH, Role.INDIRECT_JUMP, Role.TRAP}
    return (
        first_role in terminal
        and second_role in terminal
        and first.is_conditional
        and second.is_conditional
        and COMPLEMENTARY.get(first.cond) == second.cond
    )
