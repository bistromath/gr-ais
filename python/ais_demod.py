#!/usr/bin/env python
#ais_demod.py
#implements a hierarchical class to demodulate GMSK packets as per AIS, including differential decoding and bit inversion for NRZI.

from gnuradio import gr, gru, blks2
from gnuradio import eng_notation
from gnuradio import ais
from gnuradio import trellis
from gnuradio import window
from gnuradio import digital
import fsm_utils
import gmsk_sync

#from gmskenhanced import gmsk_demod
#from gmskmod import gmsk_demod
import numpy
import scipy
import scipy.stats
import math

class va_equalizer(gr.hier_block2):
	def __init__(self):
		gr.hier_block2.__init__(self, "va_equalizer",
                                gr.io_signature(1, 1, gr.sizeof_float), # Input signature
                                gr.io_signature(1, 1, gr.sizeof_char)) # Output signature
		self.modulation = (1, [1, -1]) #PAM2
		self.channel = list(gr.firdes.gaussian(1, 1, 0.35, 4))
		self.fsm = trellis.fsm(len(self.modulation[1]), len(self.channel))
		self.tot_channel = fsm_utils.make_isi_lookup(self.modulation, self.channel, True)
		self.dimensionality = self.tot_channel[0]
		self.constellation = self.tot_channel[1]
		if len(self.constellation)/self.dimensionality != self.fsm.O():
			sys.stderr.write ('Incompatible FSM output cardinality and lookup table size.\n')
			sys.exit (1)
		self.metrics = trellis.metrics_f(self.fsm.O(),
										 self.dimensionality,
										 self.constellation,
										 digital.TRELLIS_EUCLIDEAN
										)
		self.va = trellis.viterbi_b(self.fsm, 100000, -1, -1)
		self.connect(self, self.metrics, self.va, self)

class ais_demod(gr.hier_block2):
    def __init__(self, options):

		gr.hier_block2.__init__(self, "ais_demod",
                                gr.io_signature(1, 1, gr.sizeof_gr_complex), # Input signature
                                gr.io_signature(1, 1, gr.sizeof_char)) # Output signature

		self._samples_per_symbol = options.samples_per_symbol
		self._bits_per_sec = options.bits_per_sec
		self._samplerate = self._samples_per_symbol * self._bits_per_sec
		self._gain_mu = options.gain_mu
		self._mu = options.mu
		self._omega_relative_limit = options.omega_relative_limit
		self.fftlen = options.fftlen

		BT = 0.4
		data_rate = 9600.0
		samp_rate = options.samp_rate

		self.gmsk_sync = gmsk_sync.square_and_fft_sync(self._samplerate, self._bits_per_sec, self.fftlen)

		self.datafiltertaps = gr.firdes.root_raised_cosine(10, #gain
												  self._samplerate*32, #sample rate
												  self._bits_per_sec, #symbol rate
												  0.4, #alpha, same as BT?
												  50*32) #no. of taps

		self.datafilter = gr.fir_filter_fff(1, self.datafiltertaps)
		sensitivity = (math.pi / 2) / self._samples_per_symbol
		self.demod = gr.quadrature_demod_cf(sensitivity) #param is gain
		#self.clockrec = digital.clock_recovery_mm_ff(self._samples_per_symbol,0.25*self._gain_mu*self._gain_mu,self._mu,self._gain_mu,self._omega_relative_limit)
		self.clockrec = gr.pfb_clock_sync_ccf(self._samples_per_symbol, 0.04, self.datafiltertaps, 32, 0, 1.15)
		
		if(options.viterbi is True):
			self.equalizer = va_equalizer()
			self.slicer = gr.copy(gr.sizeof_char)
		else:
			self.equalizer = gr.copy(gr.sizeof_float)
			self.slicer = digital.digital.binary_slicer_fb()

		#self.tcslicer = digital.digital.binary_slicer_fb()

		self.diff = gr.diff_decoder_bb(2)
		self.invert = ais.invert() #NRZI signal diff decoded and inverted should give original signal

		self.connect(self, self.gmsk_sync)

		self.connect(self.gmsk_sync, self.clockrec, self.demod, self.equalizer, self.slicer, self.diff, self.invert, self)
		#self.connect(self.gmsk_sync, self.demod, self.clockrec, self.tcslicer, self.training_correlator)
		#self.connect(self.clockrec, self.delay, (self.dfe, 0))
		#self.connect(self.training_correlator, (self.dfe, 1))
		#self.connect(self.dfe, self.slicer, self.diff, self.invert, self)
