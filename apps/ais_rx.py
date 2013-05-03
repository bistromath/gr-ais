#!/usr/bin/env python
#ais decoder, main app
#uses demod_pkt to register a callback

#DONE: AIS freqs are fixed. Allow the user to set an error offset, and then monitor BOTH ais freqs, 161.975 and 162.025.
#something else to do: brute-force error correction, a la gr-air.
#another thing to do: use a Viterbi algorithm for detecting the demodulated data

from gnuradio import gr, gru, blks2, optfir
from gnuradio import eng_notation
from gr_ais import *
from gr_ais.ais_demod import *
from gnuradio import uhd
from gnuradio import digital
#from ais_parser import *
from optparse import OptionParser
from gnuradio.eng_option import eng_option

#from pkt import *
import time
import sys
import gnuradio.gr.gr_threading as _threading
import socket

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

class my_top_block(gr.top_block):
	def __init__(self, options, queue):
		gr.top_block.__init__(self)

		if options.filename is not None:
			self.u = gr.file_source(gr.sizeof_gr_complex, options.filename)
		else:
			self.u = uhd.usrp_source(options.addr,
									io_type=uhd.io_type.COMPLEX_FLOAT32,
									num_channels=1)
	
			if options.subdev is not None:
				self.u.set_subdev_spec(options.subdev, 0)
			self.u.set_samp_rate(options.rate)

			self._freq_offset = options.error
			#print "Frequency offset is %i" % self._freq_offset
			self._actual_freq = 162.0e6 - self._freq_offset #tune between the two AIS freqs
			#print "Tuning to: %fMHz" % float(self._actual_freq / 1e6)
			if not(self.tune(self._actual_freq)):
				print "Failed to set initial frequency"

			if options.gain is None: #set to halfway
				g = self.u.get_gain_range()
				options.gain = (g.start()+g.stop()) / 2.0

			#print "Setting gain to %i" % options.gain
                        #g = self.u.get_gain_range()
			#print "Max gain %i" % g.stop()
			#print "Min gain %i" % g.start()
			self.u.set_gain(options.gain)


		#here we're setting up TWO receivers, designated A and B. A is on 161.975, B is on 162.025. they both output data to the queue.
		self.ais_rx(self.u, 161.975e6 - 162.0e6, "A", options, queue);
		self.ais_rx(self.u, 162.025e6 - 162.0e6, "B", options, queue);

		
	def tune(self, freq):
		result = self.u.set_center_freq(freq)
		return True

		return False

	def ais_rx(self, src, freq, designator, options, queue):
		self.rate = options.rate
		self.u = src
		self.coeffs = gr.firdes.low_pass(1,self.rate,7000,1000)
		self._filter_decimation = 4
		self.filter = gr.freq_xlating_fir_filter_ccf(self._filter_decimation, 
													 self.coeffs, 
													 freq,
													 self.rate)

		self._bits_per_sec = 9600.0;

		self._samples_per_symbol = self.rate / self._filter_decimation / self._bits_per_sec

		options.samples_per_symbol = self._samples_per_symbol
		options.gain_mu = 0.3
		options.mu=0.5
		options.omega_relative_limit = 0.0001
		options.bits_per_sec = self._bits_per_sec
                #trades off accuracy of freq estimation in presence of noise, vs. delay time.
                options.fftlen = 4096
		options.samp_rate = self.rate / self._filter_decimation
                #ais_demod.py, hierarchical demodulation block, takes in complex baseband and spits out 1-bit packed bitstream
                self.demod = ais_demod(options)
		self.unstuff = ais.unstuff() #ais_unstuff.cc, unstuffs data
                #should mark start of packet
                self.start_correlator = gr.correlate_access_code_tag_bb("1010101010101010", 0, "ais_preamble")
                #should mark start and end of packet
                self.stop_correlator = gr.correlate_access_code_tag_bb("01111110", 0, "ais_frame")
                #ais_parse.cc, calculates CRC, parses data into ASCII message, moves data onto queue
                self.parse = ais.parse(queue, designator, options.verbose, options.lon, options.lat)

		self.connect(self.u,
		             self.filter,
		             self.demod,
		             self.unstuff,
		             self.start_correlator,
		             self.stop_correlator,
		             self.parse) #parse posts messages to the queue, which the main loop reads and prints


def main():
	# Create Options Parser:
	parser = OptionParser (option_class=eng_option, conflict_handler="resolve")
	expert_grp = parser.add_option_group("Expert")

	parser.add_option("-a", "--addr", type="string",
						help="UHD source address", default="")
	parser.add_option("-s", "--subdev", type="string",
						help="UHD subdev spec", default=None)
	parser.add_option("-A", "--antenna", type="string", default=None,
						help="select Rx Antenna where appropriate")
#	parser.add_option("-f", "--freq", type="eng_float", default=161.975e6,
#						help="set receive frequency to MHz [default=%default]", metavar="FREQ")
	parser.add_option("-e", "--error", type="eng_float", default=0,
						help="set offset error of USRP [default=%default]")
	parser.add_option("-g", "--gain", type="int", default=None,
						help="set RF gain", metavar="dB")
	parser.add_option("-r", "--rate", type="eng_float", default=256e3,
						help="set fgpa decimation rate to DECIM [default=%default]")
	parser.add_option("-F", "--filename", type="string", default=None,
						help="read data from file instead of USRP")
	parser.add_option("-v", "--viterbi", action="store_true", default=False,
						help="Use optional coherent demodulation and Viterbi decoder")
	parser.add_option("-t", "--tcp", action="store_true", default=False,
						help="Start a TCP server on port 9987 instead of outputting to stdout. Useful for gpsd.")
        parser.add_option("-V", "--verbose", type="int", default=1,
                                                help="Verbosity level. 0=AIS ascii output only, 1=decode packets, >1 various debug levels [default=%default]")
        parser.add_option("", "--lon", type="float", default=21.5593,
                                                help="Your longitude in decimal degrees [default=%default]")
        parser.add_option("", "--lat", type="float", default=63.1587,
                                                help="Your latitude in decimal degrees [default=%default]")

	(options, args) = parser.parse_args ()

	if len(args) != 0:
		parser.print_help(sys.stderr)
		sys.exit(1)

	# build the graph
	queue = gr.msg_queue()
	tb = my_top_block(options, queue)
	runner = top_block_runner(tb)

	conns = [] #active connections
	if options.tcp is True:
		s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
		s.bind(('', 9987))
		s.listen(1)
		s.setblocking(0)

	try:
		while 1:
			if options.tcp is True:
				try:
					conn, addr = s.accept()
					conns.append(conn)
					print "Connections: ", len(conns)
				except socket.error:
					pass
					
			if not queue.empty_p():
				while not queue.empty_p():
					msg = queue.delete_head() # Blocking read
					sentence = msg.to_string()
					if options.tcp is True:
						for conn in conns[:]:
							try:
								conn.send(sentence + "\n")
							except socket.error:
								conns.remove(conn)
								print "Connections: ", len(conns)
					else:
						print sentence
						sys.stdout.flush()

			elif runner.done:
				if options.tcp is True:
					s.close()
				break
			else:
				time.sleep(0.1)

	except KeyboardInterrupt:
		tb.stop()
		runner = None

if __name__ == '__main__':
	main()


