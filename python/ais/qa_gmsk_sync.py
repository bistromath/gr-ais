#!/usr/bin/env python
#
# Copyright 2026 Nick Foster
#
# SPDX-License-Identifier: GPL-3.0-or-later
#

import numpy as np
from gnuradio import gr, gr_unittest, blocks
from gnuradio import ais
from gnuradio.ais.qa_common import SAMP_RATE, BIT_RATE, gmsk_modulate

FFTLEN = 1024
FREQ_OFFSET_HZ = 1000.0
SETTLE_SAMPLES = 4 * FFTLEN


def residual_offset(samples):
    squared = samples * samples
    spectrum = np.abs(np.fft.fftshift(np.fft.fft(squared)))
    pair_spacing = int(len(samples) * BIT_RATE / SAMP_RATE)
    scores = spectrum[:-pair_spacing] + spectrum[pair_spacing:]
    center_bin = np.argmax(scores) + pair_spacing // 2
    return (center_bin - len(samples) // 2) * SAMP_RATE / len(samples) / 2


class qa_gmsk_sync(gr_unittest.TestCase):

    def setUp(self):
        self.tb = gr.top_block()

    def tearDown(self):
        self.tb = None

    def test_removes_frequency_offset_from_gmsk(self):
        rng = np.random.default_rng(1234)
        bits = rng.integers(0, 2, 4000).tolist()
        samples = gmsk_modulate(bits)
        rotator = np.exp(2j * np.pi * FREQ_OFFSET_HZ * np.arange(len(samples)) / SAMP_RATE)
        src = blocks.vector_source_c((samples * rotator).tolist())
        dut = ais.square_and_fft_sync_cc(SAMP_RATE, BIT_RATE, FFTLEN)
        sink = blocks.vector_sink_c()
        self.tb.connect(src, dut, sink)

        self.tb.run()

        settled = np.array(sink.data())[SETTLE_SAMPLES:]
        self.assertGreater(len(settled), 8 * FFTLEN)
        self.assertLess(abs(residual_offset(settled)), 100.0)


if __name__ == '__main__':
    gr_unittest.run(qa_gmsk_sync)
