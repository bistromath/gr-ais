#!/usr/bin/env python
#ais_demod.py
#implements a hierarchical class to demodulate GMSK packets as per AIS, including differential decoding and bit inversion for NRZI.
#does not unstuff bits

from gnuradio import gr, gru, blks2
from gnuradio import eng_notation
from gnuradio import ais
from gnuradio import trellis
#from gmskenhanced import gmsk_demod
#from gmskmod import gmsk_demod
from math import pi

class ais_demod(gr.hier_block2):
    def __init__(self, options):

		gr.hier_block2.__init__(self, "ais_demod",
                                gr.io_signature(1, 1, gr.sizeof_gr_complex), # Input signature
                                gr.io_signature(1, 1, gr.sizeof_char)) # Output signature

		self._samples_per_symbol = options.samples_per_symbol
		self._syms_per_sec = options.syms_per_sec
		self._gain_mu = options.gain_mu
		self._mu = options.mu
		self._omega_relative_limit = options.omega_relative_limit

		#you want an improvement, get some carrier tracking in there so the clockrec can work at baseband.
		#and if you're going to do that, hell, you might as well do coherent demod

		#this is probably not optimal and someone who knows what they're doing should correct me
		self.datafiltertaps = gr.firdes.root_raised_cosine(10, #gain
													  self._samples_per_symbol * self._syms_per_sec, #sample rate
													  self._syms_per_sec, #symbol rate
													  0.3, #alpha, same as BT?
													  50) #no. of taps

		self.datafilter = gr.fir_filter_fff(1, self.datafiltertaps)

		sensitivity = (pi / 2) / self._samples_per_symbol
		#print "Sensitivity is: %f" % sensitivity
		self.demod = gr.quadrature_demod_cf(sensitivity) #param is gain
		self.clockrec = gr.clock_recovery_mm_ff(self._samples_per_symbol,0.25*self._gain_mu*self._gain_mu,self._mu,self._gain_mu,self._omega_relative_limit)
		self.training_correlator = gr.correlate_access_code_bb("1100110011001100", 0)
		self.tcslicer = gr.binary_slicer_fb()

		#so the DFE block below is based on the gr.lms_dfe block. it's extended to take a "reset" input from the correlator, because AIS packets are too short to
		#train a decision-feedback equalizer adequately. so we essentially use the same packet a dozen times to train the DFE. when it gets a correlator hit,
		#it resets its taps and delay lines to zero. it then loops over the first 150 bits of the packet 12 times, training the correlator, and then
		#it runs through the whole packet (and onward) with the trained taps to cope with GMSK-induced (intentional) and channel-induced (unintentional) ISI.
		#this gets me at least 3dB of coding gain over just a binary slicer. i have not tested this in a simulated channel to figure out what the gain is, but
		#it definitely gets more hits than the binary slicer alone. for further improvement, a viterbi decoder would be a good idea.

		self.dfe = ais.extended_lms_dfe_ff(0.010, #FF tap gain
										   0.002, #FB tap gain
										   4, #FF taps
										   2) #FB taps

		self.delay = gr.delay(gr.sizeof_float, 64+14) #the correlator delays 64 bits, and the LMS delays some as well.
		self.slicer = gr.binary_slicer_fb()

		self.diff = gr.diff_decoder_bb(2)

		self.invert = ais.invert() #NRZI signal diff decoded and inverted should give original signal

		self.connect(self, self.demod, self.datafilter, self.clockrec, self.tcslicer, self.training_correlator)
		self.connect(self.clockrec, self.delay, (self.dfe, 0))
		self.connect(self.training_correlator, (self.dfe, 1))
		self.connect(self.dfe, self.slicer, self.diff, self.invert, self)

#		self.connect(self, self.demod, self.datafilter, self.clockrec, self.slicer, self.diff, self.invert, self) #uncomment this line (and comment the last four) to skip the DFE and use a slicer alone.
