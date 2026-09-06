#!/usr/bin/env python
#ais_demod.py
#implements a hierarchical class to demodulate GMSK packets as per AIS, including differential decoding and bit inversion for NRZI.
#does not unstuff bits

#modified 4/29/10 to include frequency estimation to center signals at baseband
#the coherent demodulator uses a bespoke CPM Viterbi block (viterbi_cpm_cb)
#instead of the gr-trellis VA: the carrier phase is part of its trellis state
#and decoded bits are the phase increments, so constant phase offsets are
#absorbed structurally, and a decision-directed loop tracks residual drift.
#set options["coherent"] = False for the older differential discriminator.

from gnuradio import gr
from gnuradio import digital
from gnuradio import analog
import math

from .ais_python import invert, viterbi_cpm_cb
from .cpm_trellis import gmsk_trellis, DEFAULT_TRACEBACK_LEN, DEFAULT_PHASE_GAIN
from .gmsk_sync import square_and_fft_sync_cc
class ais_demod(gr.hier_block2):
    def __init__(self, options):

        gr.hier_block2.__init__(self, "ais_demod",
                                gr.io_signature(1, 1, gr.sizeof_gr_complex), # Input signature
                                gr.io_signature(1, 1, gr.sizeof_char)) # Output signature

        self._samples_per_symbol = options[ "samples_per_symbol" ]
        self._bits_per_sec = options[ "bits_per_sec" ]
        self._samplerate = self._samples_per_symbol * self._bits_per_sec
        self._clockrec_gain = options[ "clockrec_gain" ]
        self._omega_relative_limit = options[ "omega_relative_limit" ]
        self._coherent = options.get("coherent", True)
        self.fftlen = options[ "fftlen" ]
        self.freq_sync = square_and_fft_sync_cc(self._samplerate, self._bits_per_sec, self.fftlen)
        self.agc = analog.feedforward_agc_cc(512, 2)
        self.preamble = [1,1,0,0]*7
        self.mod = digital.gmsk_mod(self._samples_per_symbol, 0.4)
        self.mod_vector = digital.modulate_vector_bc(self.mod.to_basic_block(), self.preamble, [1])
        self.preamble_detect = digital.corr_est_cc(self.mod_vector,
                                                   self._samples_per_symbol,
                                                   1, #mark delay
                                                   0.9) #threshold

        clockrec_osps = 2 if self._coherent else 1
        #the D'Andrea-Mengali generalized MSK TED operates on the CPM signal
        #directly and is seeded by the time_est tags from the correlator.
        #measured on GMSK BT=0.4 AIS bursts it acquires cleanly where the
        #TED_MENGALI_AND_DANDREA_GMSK variant drops bits at the preamble-to-
        #flag pattern transition
        self.clockrec = digital.symbol_sync_cc(digital.TED_DANDREA_AND_MENGALI_GEN_MSK,
                                               self._samples_per_symbol,
                                               self._clockrec_gain, #loop bandwidth
                                               1.0, #damping
                                               1.0, #TED gain
                                               self._omega_relative_limit * self._samples_per_symbol, #max deviation
                                               clockrec_osps)

        self.diff = digital.diff_decoder_bb(2)
        self.invert = invert() #NRZI signal diff decoded and inverted should give original signal

        if self._coherent:
            signals, next_state, _ = gmsk_trellis(clockrec_osps)
            self.trellis_demod = viterbi_cpm_cb(signals,
                                                next_state,
                                                clockrec_osps,
                                                DEFAULT_TRACEBACK_LEN,
                                                DEFAULT_PHASE_GAIN)
            self.connect(self, self.freq_sync, self.agc, (self.preamble_detect, 0),
                         self.clockrec, self.trellis_demod, self.diff, self.invert, self)
        else:
            sensitivity = (math.pi / 2)
            self.demod = analog.quadrature_demod_cf(sensitivity) #param is gain
            self.slicer = digital.binary_slicer_fb()
            self.connect(self, self.freq_sync, self.agc, (self.preamble_detect, 0),
                         self.clockrec, self.demod, self.slicer, self.diff, self.invert, self)
