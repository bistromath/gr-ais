#!/usr/bin/env python
#
# Copyright 2026 Nick Foster
#
# SPDX-License-Identifier: GPL-3.0-or-later
#

import time

import numpy as np
import pmt
from gnuradio import gr, gr_unittest, blocks, digital
from gnuradio import ais
from gnuradio.ais.qa_common import (SAMP_RATE, SPS, REFERENCE_PAYLOAD,
                                    hdlc_frame, nrzi_encode, gmsk_modulate)
from gnuradio.ais.qa_burst_sync_cc import burst_levels

#the data-aided burst receiver decodes isolated bursts on a quiet channel;
#measured threshold is ~8 dB Eb/N0, 10 dB keeps this test deterministic
EB_N0_DB = 10.0
FREQ_OFFSET_HZ = -650.0
GUARD = 1500


class qa_burst_demod(gr_unittest.TestCase):

    def setUp(self):
        self.tb = gr.top_block()

    def tearDown(self):
        self.tb = None

    def _decode(self, variant_b, seed):
        burst = gmsk_modulate(burst_levels(variant_b))
        rng = np.random.default_rng(seed)
        total = GUARD + len(burst) + GUARD
        n0 = SPS / (10.0 ** (EB_N0_DB / 10.0))
        rx = np.sqrt(n0 / 2) * (rng.standard_normal(total)
                                + 1j * rng.standard_normal(total))
        n = np.arange(len(burst))
        rx[GUARD:GUARD + len(burst)] += burst * np.exp(
            1j * (rng.uniform(0, 2 * np.pi)
                  + 2 * np.pi * FREQ_OFFSET_HZ * (n + GUARD) / SAMP_RATE))

        src = blocks.vector_source_c(rx.tolist())
        demod = ais.ais_burst_demod(SPS)
        deframer = digital.hdlc_deframer_bp(11, 64)
        debug = blocks.message_debug()
        self.tb.connect(src, demod, deframer)
        self.tb.msg_connect(deframer, "out", debug, "store")

        self.tb.run()
        deadline = time.monotonic() + 5.0
        while debug.num_messages() < 1 and time.monotonic() < deadline:
            time.sleep(0.01)

        self.assertGreaterEqual(debug.num_messages(), 1)
        return bytes(pmt.u8vector_elements(pmt.cdr(debug.get_message(0))))

    def test_decodes_isolated_burst_variant_a(self):
        decoded = self._decode(False, seed=42)

        self.assertEqual(decoded, REFERENCE_PAYLOAD)

    def test_decodes_isolated_burst_variant_b(self):
        decoded = self._decode(True, seed=43)

        self.assertEqual(decoded, REFERENCE_PAYLOAD)


if __name__ == '__main__':
    gr_unittest.run(qa_burst_demod)
