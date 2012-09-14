#!/usr/bin/env python

from gnuradio import gr
from gnuradio import eng_notation
from gnuradio import window
from gnuradio import digital
from math import pi
import gr_ais_swig as ais


class square_and_fft_sync(gr.hier_block2):
    def __init__(self, samplerate, bits_per_sec, fftlen):
		gr.hier_block2.__init__(self, "gmsk_sync",
                                gr.io_signature(1, 1, gr.sizeof_gr_complex), # Input signature
                                gr.io_signature(1, 1, gr.sizeof_gr_complex)) # Output signature

		#this is just the old square-and-fft method
		#ais.freqest is simply looking for peaks spaced bits-per-sec apart
		self.square = gr.multiply_cc(1)
		self.fftvect = gr.stream_to_vector(gr.sizeof_gr_complex, fftlen)
		self.fft = gr.fft_vcc(fftlen, True, window.rectangular(fftlen), True)
		self.freqest = ais.freqest(int(samplerate), int(bits_per_sec), fftlen)
		self.repeat = gr.repeat(gr.sizeof_float, fftlen)
		self.fm = gr.frequency_modulator_fc(-1.0/(float(samplerate)/(2*pi)))
		self.mix = gr.multiply_cc(1)

		self.connect(self, (self.square, 0))
		self.connect(self, (self.square, 1))
		#this is the feedforward branch
		self.connect(self, (self.mix, 0))
		#this is the feedback branch
		self.connect(self.square, self.fftvect, self.fft, self.freqest, self.repeat, self.fm, (self.mix, 1))
		#and this is the output
		self.connect(self.mix, self)
