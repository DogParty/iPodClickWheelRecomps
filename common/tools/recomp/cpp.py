"""Translate one discovered function body into C++ source.

The output is deliberately plain: one statement per ARM instruction, the original mnemonic as a
trailing comment, labels only where something branches to them, and braces only where an
instruction needs a temporary. Someone decompiling by hand reads this next to Ghidra's output,
so legibility matters more than cleverness.

Everything the generated code calls is declared in three hand-written headers —
`runtime/cpu.h` (registers, flags, shifter), `runtime/memory.h` (`ld32`/`st32` and friends),
`runtime/runtime.h` (`assert_trap`, `semihost`, `fatal`) — and two generated ones, `funcs.h`
(every game function) and `bindings.h` (every framework thunk and runtime entry). The mapping
from instruction to statement is summarised in PLAN.md "Generated code shape".
"""

from __future__ import annotations

from .arm import COMPARE_OPS, LOGICAL_OPS, LR, PC, SP, Instruction, Offset, Op, Operand2, ShiftType
from .cfg import FunctionBody, Role
from .functions import Function, Kind
from .image import EAppImage

COMMENT_COLUMN = 60
INDENT = "    "

REGISTER_INDEX = {SP: "SP", LR: "LR"}
SHIFT_ENUM = {
    ShiftType.LSL: "Shift::Lsl",
    ShiftType.LSR: "Shift::Lsr",
    ShiftType.ASR: "Shift::Asr",
    ShiftType.ROR: "Shift::Ror",
}
SHIFT_REG_HELPER = {
    ShiftType.LSL: "lsl_reg",
    ShiftType.LSR: "lsr_reg",
    ShiftType.ASR: "asr_reg",
    ShiftType.ROR: "ror_reg",
}


def function_symbol(function: Function) -> str:
    """The C++ name generated code uses to call `function`."""
    if function.kind is Kind.THUNK:
        return f"eapp_{function.framework}_{function.ordinal}"
    if function.kind is Kind.RUNTIME:
        return f"rt_{function.entry:08x}"
    return f"f_{function.entry:08x}"


def hex_u32(value: int) -> str:
    return f"{value & 0xFFFF_FFFF:#x}u"


class FunctionEmitter:
    """Emits one function. Create a new one per function; it holds no state across functions."""

    def __init__(
        self, body: FunctionBody, function: Function, image: EAppImage, table: dict[int, Function]
    ) -> None:
        self.body = body
        self.function = function
        self.image = image
        self.table = table

    # -- top level -------------------------------------------------------------------------

    def emit(self) -> str:
        lines = [self._header(), f"void {function_symbol(self.function)}(Cpu& cpu) {{"]
        lines.append(f"{INDENT}trace_entry({hex_u32(self.body.entry)});")
        ordered = self.body.ordered()
        labels = set(self.body.labels)
        # Instructions are emitted in address order and fall through to each other as on the
        # ARM. When the walk pulled in code below the entry (a function that jumps into the
        # middle of another, as the division helpers do), execution must still start at the
        # entry, so the body opens with a jump to it.
        if ordered and ordered[0].address != self.body.entry:
            labels.add(self.body.entry)
            lines.append(f"{INDENT}goto L_{self.body.entry:08x};")
        for insn in ordered:
            if insn.address in labels:
                lines.append(f"L_{insn.address:08x}:")
            lines.extend(self._statement_lines(insn))
        lines.append("}")
        return "\n".join(lines) + "\n"

    def _header(self) -> str:
        reached = "reached in play" if self.function.reached else "never reached in play"
        return f"// {self.function.entry:#010x} · {len(self.body.instructions)} instructions · {reached}"

    def _statement_lines(self, insn: Instruction) -> list[str]:
        statements = self._translate(insn)
        if insn.is_conditional:
            statements = self._conditional(insn, statements)
        return self._with_comment(statements, insn.text)

    @staticmethod
    def _conditional(insn: Instruction, statements: list[str]) -> list[str]:
        guard = f"if (cond_{insn.cond_name}(cpu))"
        if len(statements) == 1:
            return [f"{guard} {statements[0]}"]
        return [f"{guard} {{"] + [INDENT + s for s in statements] + ["}"]

    @staticmethod
    def _with_comment(statements: list[str], comment: str) -> list[str]:
        lines = [INDENT + s for s in statements]
        lines[0] = f"{lines[0]:<{COMMENT_COLUMN}}// {comment}"
        return lines

    # -- instruction dispatch --------------------------------------------------------------

    def _translate(self, insn: Instruction) -> list[str]:
        role = self.body.roles[insn.address]
        if role is Role.LOCAL_BRANCH:
            return [f"goto L_{insn.target:08x};"]
        if role is Role.CALL:
            return [f"cpu.r[LR] = {hex_u32(insn.next_address)};", f"{self._callee(insn.target)}(cpu);"]
        if role is Role.TAIL_CALL:
            return [f"return {self._callee(insn.target)}(cpu);"]
        if role is Role.TRAP:
            return [f"assert_trap({hex_u32(insn.address)});"]
        if role is Role.INDIRECT_CALL:
            return [
                f"call_indirect({self._reg(insn.rm if insn.op is Op.BX else insn.operand2.rm, insn)});"
            ]
        if role is Role.INDIRECT_JUMP:
            return [
                f"return call_indirect({self._reg(insn.rm if insn.op is Op.BX else insn.operand2.rm, insn)});"
            ]
        if role is Role.JUMP_TABLE:
            return self._jump_table(insn)
        if role is Role.RETURN:
            return self._return(insn)

        if insn.op is Op.DATA:
            return self._data_processing(insn)
        if insn.op is Op.MUL:
            return self._multiply(insn)
        if insn.op is Op.MULL:
            return self._multiply_long(insn)
        if insn.op in (Op.LOAD_STORE, Op.LOAD_STORE_HALF):
            return self._load_store(insn)
        if insn.op is Op.LOAD_STORE_MULTIPLE:
            return self._load_store_multiple(insn)
        if insn.op is Op.MRS:
            return [f"{self._dest(insn.rd)} = cpu.cpsr();"]
        if insn.op is Op.MSR:
            return [f"cpu.set_cpsr_flags({self._operand2_value(insn.operand2, insn)});"]
        if insn.op is Op.SVC:
            return [f"semihost(cpu, {hex_u32(insn.target)});"]
        raise NotImplementedError(f"{insn.address:#010x}: no translation for {insn.text}")

    def _callee(self, target: int) -> str:
        return function_symbol(self.table[target])

    # -- operands --------------------------------------------------------------------------

    @staticmethod
    def _reg(n: int, insn: Instruction) -> str:
        """Register read. r15 reads as the pipelined PC, a compile-time constant."""
        if n == PC:
            return hex_u32(insn.pc_value)
        return f"cpu.r[{REGISTER_INDEX.get(n, n)}]"

    @staticmethod
    def _dest(n: int) -> str:
        return f"cpu.r[{REGISTER_INDEX.get(n, n)}]"

    def _operand2_value(self, op2: Operand2, insn: Instruction) -> str:
        """The second operand as a plain expression, for instructions that do not set flags."""
        if not op2.is_register:
            return hex_u32(op2.immediate)
        base = self._reg(op2.rm, insn)
        if op2.shift_rs is not None:
            return f"{SHIFT_REG_HELPER[op2.shift]}({base}, {self._reg(op2.shift_rs, insn)})"
        return self._shift_by_immediate(base, op2.shift, op2.shift_imm)

    @staticmethod
    def _shift_by_immediate(base: str, shift: ShiftType, amount: int) -> str:
        if shift is ShiftType.LSL:
            return base if amount == 0 else f"({base} << {amount})"
        if shift is ShiftType.LSR:
            return "0u" if amount == 0 else f"({base} >> {amount})"
        if shift is ShiftType.ASR:
            return f"asr({base}, {31 if amount == 0 else amount})"
        return f"rrx({base}, cpu.c)" if amount == 0 else f"ror({base}, {amount})"

    def _operand2_with_carry(self, op2: Operand2, insn: Instruction) -> tuple[list[str], str, str]:
        """(setup statements, value expression, carry expression) for flag-setting logical ops."""
        if not op2.is_register:
            carry = "cpu.c" if op2.rotate == 0 else ("true" if op2.immediate & 0x8000_0000 else "false")
            return [], hex_u32(op2.immediate), carry
        base = self._reg(op2.rm, insn)
        if op2.shift_rs is not None:
            call = f"shift_reg({SHIFT_ENUM[op2.shift]}, {base}, {self._reg(op2.shift_rs, insn)}, cpu.c)"
        elif op2.shift is ShiftType.LSL and op2.shift_imm == 0:
            return [], base, "cpu.c"
        else:
            call = f"shift_imm({SHIFT_ENUM[op2.shift]}, {base}, {op2.shift_imm}, cpu.c)"
        return [f"const Shifted op2 = {call};"], "op2.value", "op2.carry"

    # -- data processing -------------------------------------------------------------------

    def _data_processing(self, insn: Instruction) -> list[str]:
        a = self._reg(insn.rn, insn)
        if insn.name in LOGICAL_OPS and insn.sets_flags:
            return self._logical_with_flags(insn, a)
        b = self._operand2_value(insn.operand2, insn)
        if insn.sets_flags:
            expression = {
                "add": f"add_flags(cpu, {a}, {b})",
                "adc": f"adc_flags(cpu, {a}, {b})",
                "sub": f"sub_flags(cpu, {a}, {b})",
                "sbc": f"sbc_flags(cpu, {a}, {b})",
                "rsb": f"sub_flags(cpu, {b}, {a})",
                "rsc": f"sbc_flags(cpu, {b}, {a})",
                "cmp": f"sub_flags(cpu, {a}, {b})",
                "cmn": f"add_flags(cpu, {a}, {b})",
            }[insn.name]
        else:
            expression = {
                "and": f"{a} & {b}",
                "eor": f"{a} ^ {b}",
                "sub": f"{a} - {b}",
                "rsb": f"{b} - {a}",
                "add": f"{a} + {b}",
                "adc": f"{a} + {b} + (cpu.c ? 1u : 0u)",
                "sbc": f"{a} - {b} - (cpu.c ? 0u : 1u)",
                "rsc": f"{b} - {a} - (cpu.c ? 0u : 1u)",
                "orr": f"{a} | {b}",
                "mov": b,
                "bic": f"{a} & ~{b}",
                "mvn": f"~{b}",
            }[insn.name]
        if insn.name in COMPARE_OPS:
            return [f"{expression};"]
        return [f"{self._dest(insn.rd)} = {expression};"]

    def _logical_with_flags(self, insn: Instruction, a: str) -> list[str]:
        setup, b, carry = self._operand2_with_carry(insn.operand2, insn)
        result = {
            "and": f"{a} & {b}",
            "tst": f"{a} & {b}",
            "eor": f"{a} ^ {b}",
            "teq": f"{a} ^ {b}",
            "orr": f"{a} | {b}",
            "bic": f"{a} & ~{b}",
            "mov": b,
            "mvn": f"~{b}",
        }[insn.name]
        call = f"logic_flags(cpu, {result}, {carry})"
        statement = f"{call};" if insn.name in COMPARE_OPS else f"{self._dest(insn.rd)} = {call};"
        return self._block(setup, [statement])

    @staticmethod
    def _block(setup: list[str], statements: list[str]) -> list[str]:
        if not setup:
            return statements
        return ["{"] + [INDENT + s for s in setup + statements] + ["}"]

    # -- multiplies ------------------------------------------------------------------------

    def _multiply(self, insn: Instruction) -> list[str]:
        product = f"{self._reg(insn.rm, insn)} * {self._reg(insn.rs, insn)}"
        if insn.name == "mla":
            product += f" + {self._reg(insn.rn, insn)}"
        if insn.sets_flags:
            product = f"mul_flags(cpu, {product})"
        return [f"{self._dest(insn.rd)} = {product};"]

    def _multiply_long(self, insn: Instruction) -> list[str]:
        flags = "true" if insn.sets_flags else "false"
        return [
            f"{insn.name}(cpu, {insn.rd}, {insn.rn}, {self._reg(insn.rm, insn)}, {self._reg(insn.rs, insn)}, {flags});"
        ]

    # -- loads and stores ------------------------------------------------------------------

    def _offset_value(self, offset: Offset, insn: Instruction) -> str:
        if offset.rm is None:
            return hex_u32(offset.immediate)
        return self._shift_by_immediate(self._reg(offset.rm, insn), offset.shift, offset.shift_imm)

    def _access(self, insn: Instruction, address: str) -> str:
        if insn.op is Op.LOAD_STORE_HALF:
            width = "16" if insn.half else "8"
            sign = "s" if insn.signed else ""
        else:
            width, sign = ("8" if insn.byte else "32"), ""
        if insn.load:
            return f"{self._dest(insn.rd)} = ld{width}{sign}({address});"
        # A stored r15 reads as the instruction address + 12 on this core.
        value = hex_u32(insn.address + 12) if insn.rd == PC else self._reg(insn.rd, insn)
        return f"st{width}({address}, {value});"

    def _load_store(self, insn: Instruction) -> list[str]:
        folded = self._fold_literal_load(insn)
        if folded:
            return folded
        base = self._reg(insn.rn, insn)
        offset = self._offset_value(insn.offset, insn)
        sign = "+" if insn.add_offset else "-"
        is_zero_offset = insn.offset.rm is None and insn.offset.immediate == 0
        offset_expression = base if is_zero_offset else f"{base} {sign} {offset}"

        if insn.pre_indexed and not insn.writeback:
            return [self._access(insn, offset_expression)]
        if insn.pre_indexed:
            setup = [f"const uint32_t address = {offset_expression};"]
            return self._block(setup, [self._access(insn, "address"), f"{self._dest(insn.rn)} = address;"])
        setup = [f"const uint32_t address = {base};"]
        return self._block(
            setup, [self._access(insn, "address"), f"{self._dest(insn.rn)} = address {sign} {offset};"]
        )

    def _fold_literal_load(self, insn: Instruction) -> list[str] | None:
        """`ldr rX, [pc, #imm]` reads a constant from the literal pool; emit the constant itself."""
        if not (
            insn.load and insn.rn == PC and insn.offset.rm is None and insn.pre_indexed and not insn.writeback
        ):
            return None
        address = insn.pc_value + (insn.offset.immediate if insn.add_offset else -insn.offset.immediate)
        if insn.op is Op.LOAD_STORE and not insn.byte:
            value = self.image.u32(address)
        elif insn.op is Op.LOAD_STORE_HALF and insn.half:
            value = self.image.u32(address & ~3) >> (8 * (address & 2)) & 0xFFFF
            if insn.signed and value & 0x8000:
                value -= 0x10000
        else:
            value = self.image.data[self.image.offset(address)]
            if insn.signed and value & 0x80:
                value -= 0x100
        return [f"{self._dest(insn.rd)} = {hex_u32(value)};  /* [{address:#010x}] */"]

    def _load_store_multiple(self, insn: Instruction) -> list[str]:
        registers = [i for i in range(16) if insn.register_list & (1 << i)]
        count = len(registers)
        base = self._reg(insn.rn, insn)
        if insn.add_offset:
            start = f"{base} + 4u" if insn.pre_indexed else base
            after = f"{base} + {4 * count}u"
        else:
            start = f"{base} - {4 * count}u" if insn.pre_indexed else f"{base} - {4 * count - 4}u"
            after = f"{base} - {4 * count}u"

        transfers = []
        for slot, register in enumerate(registers):
            address = "address" if slot == 0 else f"address + {4 * slot}u"
            if insn.load:
                if register == PC:
                    continue  # the popped return address is consumed by `return;` below
                transfers.append(f"{self._dest(register)} = ld32({address});")
            else:
                value = hex_u32(insn.address + 12) if register == PC else self._reg(register, insn)
                transfers.append(f"st32({address}, {value});")

        writeback = [f"{self._dest(insn.rn)} = {after};"] if insn.writeback else []
        # For loads the base is written back first so a loaded base register wins, as on the ARM.
        statements = writeback + transfers if insn.load else transfers + writeback
        block = self._block([f"const uint32_t address = {start};"], statements)
        if insn.load and PC in registers:
            block.append("return;")
        return block

    # -- control flow ----------------------------------------------------------------------

    def _return(self, insn: Instruction) -> list[str]:
        if insn.op is Op.LOAD_STORE_MULTIPLE:
            return self._load_store_multiple(insn)
        if insn.op is Op.LOAD_STORE:  # ldr pc, [sp], #4
            return self._block(
                [],
                [
                    f"{self._dest(insn.rn)} = {self._reg(insn.rn, insn)} + {hex_u32(insn.offset.immediate)};",
                    "return;",
                ],
            )
        return ["return;"]

    def _jump_table(self, insn: Instruction) -> list[str]:
        table = self.body.jump_tables[insn.address]
        lines = [f"switch ({self._reg(table.index_register, insn)}) {{"]
        for index in range(table.case_count):
            lines.append(f"case {index}: goto L_{table.case_target(index):08x};")
        lines.append(f'default: fatal("jump table at {insn.address:#010x}: index out of range");')
        lines.append("}")
        return lines
