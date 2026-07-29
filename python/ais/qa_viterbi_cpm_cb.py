#!/usr/bin/env python
#
# Copyright 2026 Nick Foster
#
# SPDX-License-Identifier: GPL-3.0-or-later
#

import numpy as np
from gnuradio import gr, gr_unittest, blocks, digital
from gnuradio import ais
from gnuradio.ais.cpm_trellis import gmsk_trellis

Q = 2
TRACEBACK = 32
MERGE_SLACK = 16


def modulated(bits):
    padded = bits + [0] * ((8 - len(bits) % 8) % 8)
    packed = np.packbits(np.array(padded, dtype=np.uint8))
    tb = gr.top_block()
    src = blocks.vector_source_b(packed.tolist())
    mod = digital.gmsk_mod(Q, 0.4)
    sink = blocks.vector_sink_c()
    tb.connect(src, mod, sink)
    tb.run()
    return np.array(sink.data())


class qa_viterbi_cpm_cb(gr_unittest.TestCase):

    def setUp(self):
        self.tb = gr.top_block()
        self.signals, self.next_state, _ = gmsk_trellis(Q)

    def tearDown(self):
        self.tb = None

    def _decode(self, samples, phase_gain=0.1):
        src = blocks.vector_source_c(samples.tolist())
        dut = ais.viterbi_cpm_cb(self.signals, self.next_state, Q,
                                 TRACEBACK, phase_gain)
        sink = blocks.vector_sink_b()
        self.tb.connect(src, dut, sink)

        self.tb.run()

        return list(sink.data())

    def test_decodes_clean_gmsk_exactly(self):
        rng = np.random.default_rng(11)
        bits = rng.integers(0, 2, 400).tolist()

        decoded = self._decode(modulated(bits))

        n = len(decoded)
        self.assertGreater(n, 300)
        self.assertEqual(decoded[MERGE_SLACK:n], bits[MERGE_SLACK:n])

    def test_absorbs_constant_carrier_phase(self):
        rng = np.random.default_rng(13)
        bits = rng.integers(0, 2, 400).tolist()
        rotated = modulated(bits) * np.exp(1j * np.radians(145))

        decoded = self._decode(rotated)

        n = len(decoded)
        self.assertEqual(decoded[MERGE_SLACK:n], bits[MERGE_SLACK:n])

    def test_rejects_mismatched_tables(self):
        with self.assertRaises(ValueError):
            ais.viterbi_cpm_cb(self.signals[:-1], self.next_state, Q,
                               TRACEBACK, 0.1)


if __name__ == '__main__':
    gr_unittest.run(qa_viterbi_cpm_cb)
