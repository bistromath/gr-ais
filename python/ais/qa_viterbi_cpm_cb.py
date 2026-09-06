#!/usr/bin/env python
#
# Copyright 2026 Nick Foster
#
# SPDX-License-Identifier: GPL-3.0-or-later
#

import numpy as np
from gnuradio import gr, gr_unittest, blocks
from gnuradio import ais
from gnuradio.ais.cpm_trellis import gmsk_trellis
from gnuradio.ais.qa_common import gmsk_modulate

Q = 2
TRACEBACK = 32
MERGE_SLACK = 16


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

        decoded = self._decode(gmsk_modulate(bits, Q))

        n = len(decoded)
        self.assertGreater(n, 300)
        self.assertEqual(decoded[MERGE_SLACK:n], bits[MERGE_SLACK:n])

    def test_absorbs_constant_carrier_phase(self):
        rng = np.random.default_rng(13)
        bits = rng.integers(0, 2, 400).tolist()
        rotated = gmsk_modulate(bits, Q) * np.exp(1j * np.radians(145))

        decoded = self._decode(rotated)

        n = len(decoded)
        self.assertEqual(decoded[MERGE_SLACK:n], bits[MERGE_SLACK:n])

    def test_rejects_mismatched_tables(self):
        with self.assertRaises(ValueError):
            ais.viterbi_cpm_cb(self.signals[:-1], self.next_state, Q,
                               TRACEBACK, 0.1)

    def test_warmup_emits_no_sentinel_bytes(self):
        rng = np.random.default_rng(17)
        bits = rng.integers(0, 2, 80).tolist()
        decoded = self._decode(gmsk_modulate(bits, Q))

        self.assertEqual(len(decoded), len(bits) - TRACEBACK + 1)
        self.assertTrue(all(bit in (0, 1) for bit in decoded))
        self.assertEqual(decoded[MERGE_SLACK:], bits[MERGE_SLACK:len(decoded)])


if __name__ == '__main__':
    gr_unittest.run(qa_viterbi_cpm_cb)
