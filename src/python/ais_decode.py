#!/usr/bin/env python
#ais decoder, main app
#uses demod_pkt to register a callback

#DONE: AIS freqs are fixed. Allow the user to set an error offset, and then monitor BOTH ais freqs, 161.975 and 162.025.
#something else to do: brute-force error correction, a la gr-air.
#another thing to do: use a Viterbi algorithm for detecting the demodulated data

from gnuradio import gr, gru, blks2, optfir
from gnuradio import eng_notation
from gnuradio import ais
from gnuradio import usrp
from ais_demod import * #use the local copy for now, not that it's particularly complicated
#from ais_parser import *
from optparse import OptionParser
from gnuradio.eng_option import eng_option
from usrpm import usrp_dbid

#from pkt import *
import time
import gnuradio.gr.gr_threading as _threading

class top_block_runner(_threading.Thread):
    def __init__(self, tb):
        _threading.Thread.__init__(self)
        self.setDaemon(1)
        self.tb = tb
        self.done = False
        self.start()

    def run(self):
        self.tb.run()
        self.done = True

def pick_subdevice(u):
#this should pick which USRP subdevice if none was specified on the command line
	return usrp.pick_subdev(u, (usrp_dbid.TV_RX_REV_3,
							usrp_dbid.TV_RX_REV_2,
				    	    usrp_dbid.BASIC_RX))

class my_top_block(gr.top_block):
	def __init__(self, options, queue):
		gr.top_block.__init__(self)

		if options.filename is not None:
			self.u = gr.file_source(gr.sizeof_gr_complex, options.filename)
			options.rate = 64e6 / options.decim #from file
		else:
			self.u = usrp.source_c()
			if options.rx_subdev_spec is None:
				options.rx_subdev_spec = pick_subdevice(self.u)
	
			self.u.set_mux(usrp.determine_rx_mux_value(self.u, options.rx_subdev_spec))
			self.subdev = usrp.selected_subdev(self.u, options.rx_subdev_spec)
			#print "Using RX d'board %s" % self.subdev.side_and_name()
			self.u.set_decim_rate(options.decim)
			options.rate = self.u.adc_rate() / options.decim

			self._freq_offset = options.error
			#print "Frequency offset is %i" % self._freq_offset
			self._actual_freq = 162.0e6 - self._freq_offset #tune between the two AIS freqs
			#print "Tuning to: %fMHz" % float(self._actual_freq / 1e6)
			if not(self.tune(self._actual_freq)):
				print "Failed to set initial frequency"

			if options.gain is None: #set to halfway
				g = self.subdev.gain_range()
				options.gain = (g[0]+g[1]) / 2.0

			#print "Setting gain to %i" % options.gain
			self.subdev.set_gain(options.gain)
			#self.subdev.set_bw(self.options.bandwidth) #only for DBSRX


		#here we're setting up TWO receivers, designated A and B. A is on 161.975, B is on 162.025. they both output data to the queue.

		self.ais_rx(self.u, 161.975e6 - 162.0e6, "A", options, queue);
		self.ais_rx(self.u, 162.025e6 - 162.0e6, "B", options, queue);

		
	def tune(self, freq):
		result = usrp.tune(self.u, 0, self.subdev, freq)
		if result:
			# Use residual_freq in s/w freq translater
			#self.ddc.set_center_freq(-result.residual_freq)
			#print "residual_freq =", result.residual_freq
			return True

		return False

	def ais_rx(self, src, freq, designator, options, queue):
		self.rate = options.rate
		#print "Samples per second is %i" % self.rate
		self.u = src
		self.coeffs = gr.firdes.low_pass(1,self.rate,10000,1000)
		self._filter_decimation = 4
		self.filter = gr.freq_xlating_fir_filter_ccf(self._filter_decimation, 
													 self.coeffs, 
													 freq,
													 self.rate)

		self._syms_per_sec = 9600.0;

		self._samples_per_symbol = self.rate / self._filter_decimation / self._syms_per_sec
		#print "Samples per symbol is %f" % (self._samples_per_symbol,)

		options.samples_per_symbol = self._samples_per_symbol
		options.gain_mu = 0.5
		options.mu=0.5
		options.omega_relative_limit = 0.0001
		options.syms_per_sec = self._syms_per_sec

		self.demod = ais_demod(options) #ais_demod.py, hierarchical demodulation block, takes in complex baseband and spits out 1-bit packed bitstream
		self.start_correlator = gr.correlate_access_code_bb("1010101010101010", 0) #should mark start of packet
		self.stop_correlator = gr.correlate_access_code_bb("01111110", 0) #should mark start and end of packet
		self.shift = ais.shift(); #used to combine the output of two correlators (start_correlator and stop_correlator), moves bit 1 -> bit 2
		self.unstuff = ais.unstuff() #ais_unstuff.cc, unstuffs data
		self.parse = ais.parse(queue, designator) #ais_parse.cc, calculates CRC, parses data into ASCII message, moves data onto queue

		self.connect(self.u, self.filter, self.demod, self.start_correlator, (self.shift, 0))#now, ais.shift takes the output of the first correlator and moves the flag to the second bit
		self.connect(self.demod, self.stop_correlator, (self.shift, 1)) #so we can encode another correlator flag bit into the stream
		self.connect(self.shift, self.unstuff, self.parse) #parse posts messages to the queue, which the main loop reads and prints



def main():
	global n_rcvd, n_right

	n_rcvd = 0
	n_right = 0

	# Create Options Parser:
	parser = OptionParser (option_class=eng_option, conflict_handler="resolve")
	expert_grp = parser.add_option_group("Expert")

	parser.add_option("-R", "--rx-subdev-spec", type="subdev",
						help="select USRP Rx side A or B", metavar="SUBDEV")
#	parser.add_option("-f", "--freq", type="eng_float", default=161.975e6,
#						help="set receive frequency to MHz [default=%default]", metavar="FREQ")
	parser.add_option("-e", "--error", type="eng_float", default=0,
						help="set offset error of USRP [default=%default]")
	parser.add_option("-g", "--gain", type="int", default=None,
						help="set RF gain", metavar="dB")
	parser.add_option("-d", "--decim", type="int", default=256,
						help="set fgpa decimation rate to DECIM [default=%default]")
	parser.add_option("-F", "--filename", type="string", default=None,
						help="read data from file instead of USRP")

	#receive_path.add_options(parser, expert_grp)

	(options, args) = parser.parse_args ()

	if len(args) != 0:
		parser.print_help(sys.stderr)
		sys.exit(1)

	# build the graph
	queue = gr.msg_queue()
	tb = my_top_block(options, queue)
	runner = top_block_runner(tb)

	try:
		while 1:
			if not queue.empty_p():
				msg = queue.delete_head() # Blocking read
				sentence = msg.to_string()
				#s = make_printable(sentence)
				print sentence
#		tb.adjust_freq()
			elif runner.done:
				break
			else:
				time.sleep(0.1)

#		tb.run()

	except KeyboardInterrupt:
		tb.stop()
		runner = None

if __name__ == '__main__':
	main()


