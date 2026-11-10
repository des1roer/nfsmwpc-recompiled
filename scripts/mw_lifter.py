"""Project-local corrections for the shared pcrecomp lifter.

FCOM/FUCOM/FTST update the x87 status word, not integer EFLAGS. MW's
0x81CDC6 sound routine schedules CMP channel,-1; FUCOMPP; JNE: replacing
the CMP flags sends the all-channel sentinel through a single-channel write,
overwriting its effects callback at voice+0x118 with a floating-point gain.
Keep the integer lazy flags until TEST/SAHF explicitly transfers the status.

XADD/CMPXCHG (dword): the shared lift32 strips LOCK but has no case for these
two, so they lifted as "Instruction unsupported". speed.exe's interlocked
reference counts (0x834D9E..0x8357D0, reached at boot through the EZ Wheel
Wrapper's DirectInput device setup; issue codepdbh/nfsmwpc-recompiled#773)
use them. Lifted code runs under one machine lock, so the plain read-modify-
write is exact, as for the other LOCK instructions.
"""
from capstone.x86 import X86_OP_REG
from lift32 import Lifter as BaseLifter, op_bits


class Lifter(BaseLifter):
    def _lift_instruction(self, insn):
        mnemonic = insn.mnemonic
        base = mnemonic[4:].strip() if mnemonic.startswith('lock') else mnemonic
        ops = insn.operands if insn.operands else []
        if base in ('xadd', 'cmpxchg') and len(ops) == 2 and op_bits(ops[0]) == 32 \
                and ops[1].type == X86_OP_REG:
            return self._lift_interlocked(insn, base, ops)
        if mnemonic not in {'fcom', 'fcomp', 'fcompp',
                            'fucom', 'fucomp', 'fucompp', 'ftst'}:
            return super()._lift_instruction(insn)
        state, seq = self._flag_state, self._flag_seq
        lines = super()._lift_instruction(insn)
        self._flag_state, self._flag_seq = state, seq
        return [line for line in lines
                if line != '_flag_a = (uint32_t)_fpu_cmp; _flag_b = 0;']

    def _lift_interlocked(self, insn, base, ops):
        comment = f"/* 0x{insn.address:08X}: {insn.mnemonic} {insn.op_str} */"
        dst = self._fmt_read(ops[0])
        src = self._fmt_read(ops[1])
        if base == 'xadd':
            # dst = dst + src (flags as add), src = old dst. Memory is written
            # first: its address never uses the source register here, but the
            # register write must not move the address before the store.
            lines = [f"{{ uint32_t _xold = {dst}; uint32_t _xsrc = {src}; {comment}",
                     "  " + self._flag_capture("_xold", "_xsrc", 32),
                     "  _cf = (uint32_t)((uint32_t)(_flag_a + _flag_b) < (uint32_t)_flag_a);",
                     f"  {self._fmt_write(ops[0], '_xold + _xsrc')};",
                     f"  {self._fmt_write(ops[1], '_xold')}; }}"]
            self._flag_state = ('add', "_flag_a, _flag_b")
            return lines
        # cmpxchg dst, src: flags as cmp eax, dst; equal -> dst = src, else eax = dst.
        lines = [f"{{ uint32_t _cold = {dst}; uint32_t _cacc = eax; {comment}",
                 "  " + self._flag_capture("_cacc", "_cold", 32),
                 "  _cf = (uint32_t)CMP_B(_flag_a, _flag_b);",
                 f"  if (_cacc == _cold) {{ {self._fmt_write(ops[0], src)}; }} else {{ eax = _cold; }} }}"]
        self._flag_state = ('cmp', "_flag_a, _flag_b")
        return lines
