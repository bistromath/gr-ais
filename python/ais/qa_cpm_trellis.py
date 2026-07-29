#!/usr/bin/env python
#
# Copyright 2026 Nick Foster
#
# SPDX-License-Identifier: GPL-3.0-or-later
#

import numpy as np
from gnuradio import gr_unittest
from gnuradio.ais.cpm_trellis import gmsk_trellis
from gnuradio.ais.qa_common import SPS, gmsk_modulate


class qa_cpm_trellis(gr_unittest.TestCase):

    def test_chained_branch_signals_reproduce_gmsk_mod(self):
        signals, next_state, num_states = gmsk_trellis(SPS)
        table = np.array(signals).reshape(len(next_state), SPS)
        rng = np.random.default_rng(7)
        bits = rng.integers(0, 2, 200).tolist()
        reference = gmsk_modulate(bits)

        state = 0
        synthesized = []
        for bit in bits:
            branch = state * 2 + bit
            synthesized.append(table[branch])
            state = next_state[branch]
        synthesized = np.concatenate(synthesized)

        memory = int(np.log2(len(next_state) // 8)) + 1
        skip = memory * SPS
        n = min(len(reference), len(synthesized))
        rotation = (reference[skip:n] * synthesized[skip:n].conj()).mean()
        rotation /= abs(rotation)
        error = np.abs(reference[skip:n] - synthesized[skip:n] * rotation).max()
        self.assertLess(error, 1e-3)
        self.assertEqual(num_states * 2, len(next_state))
        self.assertTrue(np.allclose(np.abs(table), 1.0, atol=1e-5))


if __name__ == '__main__':
    gr_unittest.run(qa_cpm_trellis)
