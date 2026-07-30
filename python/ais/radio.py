# Copyright 2013 Nick Foster
# 
# This file is part of gr-ais
# 
# gr-ais is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3, or (at your option)
# any later version.
# 
# gr-ais is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
# 
# You should have received a copy of the GNU General Public License
# along with gr-ais; see the file COPYING.  If not, write to
# the Free Software Foundation, Inc., 51 Franklin Street,
# Boston, MA 02110-1301, USA.
#

# Radio interface for AIS receiver
# Handles all hardware- and source-related functionality
# You pass it options, it gives you data.
# It uses the pubsub interface to allow clients to subscribe to its data feeds.

from gnuradio import gr, filter, blocks, digital, network
from gnuradio.filter import pfb
from gnuradio.eng_option import eng_option
from gnuradio.gr.pubsub import pubsub
from optparse import OptionParser, OptionGroup
import re

from .ais_demod import ais_demod
from .ais_python import pdu_to_nmea

AIS_CENTER_FREQ = 162.0e6
AIS_CHANNEL_A_FREQ = 161.975e6
AIS_CHANNEL_B_FREQ = 162.025e6
SOAPY_CHANNEL = 0
UDP_HEADERTYPE_NONE = 0
UDP_PAYLOAD_SIZE = 1472 #max payload of a standard 1500 byte Ethernet frame

#hier block encapsulating all the signal processing after the source
#could probably be split into its own file
class ais_rx(gr.hier_block2):
    def __init__(self, freq, rate, designator):
        gr.hier_block2.__init__(self,
                                "ais_rx",
                                gr.io_signature(1,1,gr.sizeof_gr_complex),
                                gr.io_signature(0,0,0))

        self._bits_per_sec = 9600.0
        self._samples_per_symbol = 5
        self.coeffs = filter.firdes.low_pass(1, rate, 11000, 1000)
        self._filter_decimation = int(rate/(self._bits_per_sec*self._samples_per_symbol))
        self.filter = filter.freq_xlating_fir_filter_ccf(self._filter_decimation,
                                                     self.coeffs,
                                                     freq,
                                                     rate)
#        self.resamp = pfb.arb_resampler_ccf((self._bits_per_sec*self._samples_per_symbol)/int(rate/self._filter_decimation))
        options = {}
        options[ "samples_per_symbol" ] = (rate/self._filter_decimation)/self._bits_per_sec
        options[ "clockrec_gain" ] = 0.01
        options[ "omega_relative_limit" ] = 0.01
        options[ "bits_per_sec" ] = self._bits_per_sec
        options[ "fftlen" ] = 1024 #trades off accuracy of freq estimation in presence of noise, vs. delay time.
        options[ "samp_rate" ] = self._bits_per_sec * self._samples_per_symbol
        options[ "coherent" ] = True
        self.demod = ais_demod(options) #ais_demod takes in complex baseband and spits out 1-bit unpacked bitstream
        self.deframer = digital.hdlc_deframer_bp(11,64) #takes bytes, deframes, unstuffs, CRCs, and emits PDUs with frame contents
        self.nmea = pdu_to_nmea(designator) #turns data PDUs into NMEA sentences
#        self.msgq = ais.pdu_to_msgq(queue) #posts PDUs to message queue for main program to parse at will
#        self.parse = ais.parse(queue, designator) #ais_parse.cc, calculates CRC, parses data into NMEA AIVDM message, moves data onto queue

        self.connect(self,
                     self.filter,
                     self.demod,
                     self.deframer)
        self.msg_connect(self.deframer, "out", self.nmea, "print")

def _tune(src, freq, error_ppm):
    if src.has_frequency_correction(SOAPY_CHANNEL):
        src.set_frequency_correction(SOAPY_CHANNEL, error_ppm)
        src.set_frequency(SOAPY_CHANNEL, freq)
    else: #no hardware correction available, so pull the tuner off frequency instead
        src.set_frequency(SOAPY_CHANNEL, freq * (1 + error_ppm/1.e6))
    print("Tuned to %.3fMHz" % (src.get_frequency(SOAPY_CHANNEL) / 1.e6))

def _center_freq(options):
    return AIS_CHANNEL_A_FREQ if options.singlechannel else AIS_CENTER_FREQ

def _set_gain(src, gain, agc):
    if src.has_gain_mode(SOAPY_CHANNEL):
        src.set_gain_mode(SOAPY_CHANNEL, agc)
    elif agc:
        raise Exception("Device does not support hardware AGC")

    if agc:
        print("Using hardware AGC")
        return

    if gain is None: #set to halfway
        gain_range = src.get_gain_range(SOAPY_CHANNEL)
        gain = (gain_range.minimum() + gain_range.maximum()) / 2.0
    src.set_gain(SOAPY_CHANNEL, gain)
    print("Gain is %.1fdB" % src.get_gain(SOAPY_CHANNEL))

class ais_radio (gr.top_block, pubsub):
  def __init__(self, options):
    gr.top_block.__init__(self)
    pubsub.__init__(self)
    self._options = options

    self._u = self._setup_source(options)
    self._rate = self.get_rate()
    print("Rate is %i" % (self._rate,))

    center_freq = _center_freq(options)
    channel_freqs = (AIS_CHANNEL_A_FREQ,) if options.singlechannel \
                    else (AIS_CHANNEL_A_FREQ, AIS_CHANNEL_B_FREQ)
    self._rx_paths = tuple(ais_rx(freq - center_freq, options.rate, designator)
                           for freq, designator in zip(channel_freqs, ("A", "B")))
    for rx_path in self._rx_paths:
        self.connect(self._u, rx_path)

    #now subscribe to set various options via pubsub
    self.subscribe("gain", self.set_gain)

    self.publish("gain", self.get_gain)
    self.publish("rate", self.get_rate)

  @staticmethod
  def add_radio_options(parser):
    group = OptionGroup(parser, "Receiver setup options")

    #Choose source
    group.add_option("-s","--source", type="string", default="soapy",
                      help="Choose source: soapy, <filename>, or udp:<port> [default=%default]")

    #SoapySDR args
    group.add_option("-D", "--args", type="string", default="",
                      help='SoapySDR device args, e.g. "driver=rtlsdr", "driver=airspy", '
                           '"driver=uhd,type=b200" [default=first device found]')
    group.add_option("-R", "--subdev", type="string",
                      help="select frontend mapping, e.g. \"A:0\" on a USRP", metavar="SUBDEV")
    group.add_option("-A", "--antenna", type="string",
                      help="select which antenna to use")
    group.add_option("-g", "--gain", type="eng_float", default=None,
                      help="set RF gain [default=halfway]", metavar="dB")
    group.add_option("--agc", action="store_true", default=False,
                      help="use hardware AGC instead of a fixed gain [default=%default]")
    group.add_option("-e", "--error", type="eng_float", default=0,
                      help="set offset error of device in PPM [default=%default]")
    #RX path args
    group.add_option("-r", "--rate", type="eng_float", default=250e3,
                      help="set sample rate [default=%default]")
    group.add_option("-S", "--singlechannel", action="store_true", default=False,
                     help="Use only a single channel instead of looking at both A & B [default=%default]")

    parser.add_option_group(group)

  def live_source(self):
    return self._options.source == "soapy"

  def set_gain(self, gain):
    if self.live_source():
        _set_gain(self._u, gain, False)
    return self.get_gain()

  def get_gain(self):
    return self._u.get_gain(SOAPY_CHANNEL) if self.live_source() else 0

  def get_rate(self):
    return self._u.get_sample_rate(SOAPY_CHANNEL) if self.live_source() else self._rate

  def _setup_source(self, options):
    if options.source == "soapy":
      return self._setup_soapy_source(options)

    self._rate = options.rate
    return self._setup_offline_source(options)

  @staticmethod
  def _setup_soapy_source(options):
    from gnuradio import soapy #gr-soapy ships separately, so only demand it for live sources
    src = soapy.source(options.args, "fc32", 1, "", "", [""], [""])
    print("Using SoapySDR device %s" % (options.args if options.args else "(first found)",))

    if options.subdev is not None:
      src.set_frontend_mapping(options.subdev)
    if options.antenna is not None:
      src.set_antenna(SOAPY_CHANNEL, options.antenna)

    src.set_sample_rate(SOAPY_CHANNEL, options.rate)
    _tune(src, _center_freq(options), options.error)
    _set_gain(src, options.gain, options.agc)
    return src

  @staticmethod
  def _setup_offline_source(options):
    udp = re.match(r"udp:(\d{1,5})$", options.source)
    if udp is not None:
      port = int(udp.group(1))
      print("Listening for complex samples on UDP port %i" % (port,))
      return network.udp_source(gr.sizeof_gr_complex, 1, port, UDP_HEADERTYPE_NONE,
                                UDP_PAYLOAD_SIZE, False, False, False)

    print("Using file source %s" % options.source)
    return blocks.file_source(gr.sizeof_gr_complex, options.source)
