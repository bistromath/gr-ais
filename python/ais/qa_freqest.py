#!/usr/bin/env python
#
# Copyright 2026 Nick Foster
#
# SPDX-License-Identifier: GPL-3.0-or-later
#

from gnuradio import gr, gr_unittest, blocks
from gnuradio import ais

FFTLEN = 1024
SAMPLE_RATE = 61440.0
DATA_RATE = 9600
BIN_SPACING = SAMPLE_RATE / FFTLEN
PAIR_SPACING = int(FFTLEN * DATA_RATE / SAMPLE_RATE)


def shifted_fft_with_tone_pair(first_bin):
    vec = [0j] * FFTLEN
    vec[first_bin] = 1.0 + 0j
    vec[first_bin + PAIR_SPACING] = 1.0 + 0j
    return vec


class qa_freqest(gr_unittest.TestCase):

    def setUp(self):
        self.tb = gr.top_block()

    def tearDown(self):
        self.tb = None

    def test_reports_half_the_tone_pair_center_frequency(self):
        offset_pair = shifted_fft_with_tone_pair(560)
        centered_pair = shifted_fft_with_tone_pair(FFTLEN // 2 - PAIR_SPACING // 2)
        src = blocks.vector_source_c(offset_pair + centered_pair, False, FFTLEN)
        dut = ais.freqest(SAMPLE_RATE, DATA_RATE, FFTLEN)
        sink = blocks.vector_sink_f()
        self.tb.connect(src, dut, sink)

        self.tb.run()

        expected_offset = (560 + PAIR_SPACING // 2 - FFTLEN // 2) * BIN_SPACING / 2
        self.assertFloatTuplesAlmostEqual(sink.data(), (expected_offset, 0.0))


if __name__ == '__main__':
    gr_unittest.run(qa_freqest)
