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
from gnuradio import filter as grfilter
from gnuradio import ais
from gnuradio.ais.qa_common import (SAMP_RATE, BIT_RATE, SPS,
                                    REFERENCE_PAYLOAD, hdlc_frame,
                                    nrzi_encode, gmsk_modulate)

#measured decode threshold of the current demodulator is ~15 dB Eb/N0
#(differential detection plus square-and-FFT frequency sync); 20 dB keeps
#this regression test deterministic until the sensitivity work happens
EB_N0_DB = 20.0
FREQ_OFFSET_HZ = 500.0
TIMING_OFFSET_SAMPLES = 2.3
DEMOD_OPTIONS = {
    "samples_per_symbol": SAMP_RATE / BIT_RATE,
    "clockrec_gain": 0.04,
    "omega_relative_limit": 0.01,
    "bits_per_sec": BIT_RATE,
    "fftlen": 1024,
    "samp_rate": SAMP_RATE,
}


def ais_burst(payload):
    training = [0, 1] * 1000
    preamble = [0, 1] * 12
    tail = [0, 1] * 750
    return gmsk_modulate(nrzi_encode(training + preamble + hdlc_frame(payload) + tail))


def channel(samples, eb_n0_db, freq_offset_hz, timing_offset, seed):
    fractional_bins = np.fft.fftfreq(len(samples))
    delayed = np.fft.ifft(np.fft.fft(samples)
                          * np.exp(-2j * np.pi * fractional_bins * timing_offset))
    rotator = np.exp(2j * np.pi * freq_offset_hz * np.arange(len(samples)) / SAMP_RATE)
    n0 = SPS / (10.0 ** (eb_n0_db / 10.0))
    rng = np.random.default_rng(seed)
    noise = np.sqrt(n0 / 2) * (rng.standard_normal(len(samples))
                               + 1j * rng.standard_normal(len(samples)))
    return delayed * rotator + noise


class qa_ais_demod(gr_unittest.TestCase):

    def setUp(self):
        self.tb = gr.top_block()

    def tearDown(self):
        self.tb = None

    def test_decodes_ais_frame_in_awgn(self):
        samples = channel(ais_burst(REFERENCE_PAYLOAD), EB_N0_DB,
                          FREQ_OFFSET_HZ, TIMING_OFFSET_SAMPLES, seed=1)
        src = blocks.vector_source_c(samples.tolist())
        channel_filter = grfilter.fir_filter_ccf(
            1, grfilter.firdes.low_pass(1, SAMP_RATE, 11000, 1000))
        demod = ais.ais_demod(DEMOD_OPTIONS)
        deframer = digital.hdlc_deframer_bp(11, 64)
        debug = blocks.message_debug()
        self.tb.connect(src, channel_filter, demod, deframer)
        self.tb.msg_connect(deframer, "out", debug, "store")

        self.tb.run()
        deadline = time.monotonic() + 5.0
        while debug.num_messages() < 1 and time.monotonic() < deadline:
            time.sleep(0.01)

        self.assertGreaterEqual(debug.num_messages(), 1)
        decoded = bytes(pmt.u8vector_elements(pmt.cdr(debug.get_message(0))))
        self.assertEqual(decoded, REFERENCE_PAYLOAD)


if __name__ == '__main__':
    gr_unittest.run(qa_ais_demod)
