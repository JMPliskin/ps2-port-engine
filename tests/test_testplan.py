"""Test cases the original cannot finish (testplan.run_cases).

Such a case - a loop that runs past the budget on this input, a trap - is left
out rather than failing the function; every case that ran is still compared,
and a disagreement on any of them still fails it.
"""

import os
import struct
import sys
import unittest

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
sys.path[:0] = [ROOT, os.path.join(ROOT, "tools")]

from ps2decomp.emu import Machine  # noqa: E402
from ps2decomp import testplan  # noqa: E402

BASE = 0x00100000
ZERO, V0, A0, T0, RA = 0, 2, 4, 8, 31


def i_type(op, rs, rt, imm):
    return (op << 26) | (rs << 21) | (rt << 16) | (imm & 0xFFFF)


def f_words():
    """f(n): while (n & 1) {} return n + 1  - for ever on an odd n."""
    return [
        i_type(0x0C, A0, T0, 1),         # andi $t0, $a0, 1
        i_type(0x05, T0, ZERO, -1),      # bne $t0, $zero, back to itself
        0,
        (RA << 21) | 0x08,               # jr $ra
        i_type(0x09, A0, V0, 1),         # addiu $v0, $a0, 1
    ]


class TestRunCases(unittest.TestCase):
    def setUp(self):
        words = f_words()
        self.image = struct.pack("<%dI" % len(words), *words)
        self.machine = Machine(BASE, self.image, 0x02000000 - BASE)
        self.func = {"addr": BASE, "end": BASE + 4 * len(words), "size": 4 * len(words)}

    def run_cases(self, arguments, evaluate, record=None, recorded=None):
        arg_sets = [(n,) + (0,) * 15 for n in arguments]
        return testplan.run_cases(self.func, self.machine, arg_sets, "zero", evaluate,
                                  record=record, recorded=recorded)

    def test_cases_the_original_cannot_finish_are_left_out(self):
        record = []
        outcome, tested = self.run_cases([0, 1, 2, 3, 4, 6, 8, 10],
                                         lambda args: (args[0] + 1, []), record)
        self.assertEqual((outcome, tested), ("ok", 6))
        self.assertEqual([r is None for r in record], [False, True, False, True] + [False] * 4)
        # Replayed from the record (the compiled check), the same cases are left out.
        self.assertEqual(self.run_cases([0, 1, 2, 3, 4, 6, 8, 10], lambda args: (args[0] + 1, []),
                                        recorded=record), ("ok", 6))

    def test_a_disagreement_on_a_case_that_ran_still_fails(self):
        outcome, _tested = self.run_cases([1, 3, 0, 2, 4, 6, 8, 10], lambda args: (args[0] + 2, []))
        self.assertEqual(outcome, "mismatch")


if __name__ == "__main__":
    unittest.main()
