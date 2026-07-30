#!/usr/bin/env python
#
# Copyright 2026 Nick Foster
#
# SPDX-License-Identifier: GPL-3.0-or-later
#

import numpy as np
import pmt
from gnuradio import gr, gr_unittest, blocks
from gnuradio import ais
from gnuradio.ais.cpm_trellis import (gmsk_burst_reference, RAMP_SYMBOLS,
                                      PREAMBLE_SYMBOLS)
from gnuradio.ais.qa_common import (SAMP_RATE, SPS, REFERENCE_PAYLOAD,
                                    hdlc_frame, nrzi_encode, gmsk_modulate)

GUARD = 1000
FREQ_OFFSET_HZ = 437.0
TAG_SYMBOL = RAMP_SYMBOLS + PREAMBLE_SYMBOLS - 1


def burst_levels(variant_b):
    bits = [0, 1] * ((RAMP_SYMBOLS + PREAMBLE_SYMBOLS) // 2) + hdlc_frame(REFERENCE_PAYLOAD)
    levels = nrzi_encode(bits)
    return [1 - lvl for lvl in levels] if variant_b else levels


class qa_burst_sync_cc(gr_unittest.TestCase):

    def setUp(self):
        self.tb = gr.top_block()
        (self.reference, self.tag_offset,
         self.seed_a, self.seed_b) = gmsk_burst_reference(SPS)

    def tearDown(self):
        self.tb = None

    def _residual_offset(self, out, burst):
        delay = 2 * len(self.reference)
        received = out[GUARD + delay:GUARD + delay + len(burst)]
        wiped = received[:len(burst)] * np.conj(burst[:len(received)])
        drift = np.angle((wiped[1:] * np.conj(wiped[:-1])).sum())
        return abs(drift) * SAMP_RATE / (2 * np.pi)

    def _check(self, variant_b, expected_seed):
        burst = gmsk_modulate(burst_levels(variant_b))
        rx = np.concatenate([np.zeros(GUARD), burst, np.zeros(GUARD)])
        n = np.arange(len(rx))
        rx = rx * np.exp(1j * (0.7 + 2 * np.pi * FREQ_OFFSET_HZ * n / SAMP_RATE))
        src = blocks.vector_source_c(rx.tolist())
        dut = ais.burst_sync_cc(self.reference, SAMP_RATE,
                                [-800.0, -400.0, 0.0, 400.0, 800.0],
                                0.4, self.tag_offset, self.seed_a, self.seed_b,
                                300 * SPS)
        sink = blocks.vector_sink_c()
        self.tb.connect(src, dut, sink)

        self.tb.run()

        state_tags = [t for t in sink.tags()
                      if pmt.symbol_to_string(t.key) == "burst_state"]
        self.assertEqual(len(state_tags), 1)
        self.assertEqual(pmt.to_long(state_tags[0].value), expected_seed)
        expected_offset = GUARD + TAG_SYMBOL * SPS + 2 * len(self.reference)
        self.assertEqual(int(state_tags[0].offset), expected_offset)

        self.assertLess(self._residual_offset(np.array(sink.data()), burst), 40.0)

    def test_detects_variant_a_and_derotates(self):
        self._check(False, self.seed_a)

    def test_detects_variant_b_and_derotates(self):
        self._check(True, self.seed_b)


if __name__ == '__main__':
    gr_unittest.run(qa_burst_sync_cc)
