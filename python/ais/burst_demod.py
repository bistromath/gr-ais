#
# Copyright 2026 Nick Foster
#
# SPDX-License-Identifier: GPL-3.0-or-later
#

# Data-aided burst demodulator: preamble+flag correlation for detection,
# timing, phase, frequency, and NRZI variant, feeding a seeded CPM Viterbi.
# Handles isolated bursts on a quiet channel, which the square-and-FFT
# frequency sync in ais_demod cannot. Input must be complex baseband at an
# integer number of samples per symbol.

import numpy as np
from gnuradio import gr
from gnuradio import digital

from .ais_python import burst_sync_cc, viterbi_cpm_cb, invert
from .cpm_trellis import (gmsk_trellis, gmsk_burst_reference, SLOT_SYMBOLS,
                          REFERENCE_SYMBOLS, DEFAULT_TRACEBACK_LEN,
                          DEFAULT_PHASE_GAIN)

HYPOTHESIS_SPACING_HZ = 200.0


class ais_burst_demod(gr.hier_block2):
    '''Decode bursts separated by at least burst_slots AIS slots.

    The default permits adjacent single-slot transmissions. For captures
    with known longer bursts, increase burst_slots to suppress detections
    within their payloads. This is a minimum-spacing assumption, not an
    automatic frame-length detector.
    '''
    def __init__(self, samples_per_symbol, bits_per_sec=9600.0,
                 freq_span=1200.0, threshold=0.4, burst_slots=1):
        gr.hier_block2.__init__(self, "ais_burst_demod",
                                gr.io_signature(1, 1, gr.sizeof_gr_complex),
                                gr.io_signature(1, 1, gr.sizeof_char))

        sps = int(samples_per_symbol)
        if sps < 1 or sps != samples_per_symbol:
            raise ValueError("samples_per_symbol must be a positive integer")
        if burst_slots < 1 or int(burst_slots) != burst_slots:
            raise ValueError("burst_slots must be a positive integer")

        signals, next_state, _ = gmsk_trellis(sps)
        reference, tag_offset, seed_a, seed_b = gmsk_burst_reference(sps)
        hypotheses = np.arange(-freq_span, freq_span + 1.0,
                               HYPOTHESIS_SPACING_HZ).tolist()

        # Resume one reference window before the next expected peak, so
        # its preamble shoulders enter the synchronizer's peak search.
        holdoff = (int(burst_slots) * SLOT_SYMBOLS - REFERENCE_SYMBOLS) * sps
        self.sync = burst_sync_cc(reference,
                                  sps * bits_per_sec,
                                  hypotheses,
                                  threshold,
                                  tag_offset,
                                  seed_a,
                                  seed_b,
                                  holdoff)
        self.trellis_demod = viterbi_cpm_cb(signals,
                                            next_state,
                                            sps,
                                            DEFAULT_TRACEBACK_LEN,
                                            DEFAULT_PHASE_GAIN)
        self.diff = digital.diff_decoder_bb(2)
        self.invert = invert()

        self.connect(self, self.sync, self.trellis_demod,
                     self.diff, self.invert, self)
