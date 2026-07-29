#!/usr/bin/env python
#
# Copyright 2026 Nick Foster
#
# SPDX-License-Identifier: GPL-3.0-or-later
#

import time

import pmt
from gnuradio import gr, gr_unittest, blocks
from gnuradio import ais
from gnuradio.ais.qa_common import REFERENCE_PAYLOAD, REFERENCE_SENTENCE

PADDED_TWO_FRAGMENT_PAYLOAD = bytes(range(46))
PADDED_TWO_FRAGMENT_SENTENCES = (
    "!AIVDM,2,1,,A,00420h@51PL82@`;30l>3i0A4Q<D5AHG61TJ6ihM7QtP8B8S92DV9jPa,4*61\n"
    "!AIVDM,2,2,,A,:Rdd;@,4*31")


class qa_pdu_to_nmea(gr_unittest.TestCase):

    def setUp(self):
        self.tb = gr.top_block()

    def tearDown(self):
        self.tb = None

    def _to_nmea(self, designator, payload):
        dut = ais.pdu_to_nmea(designator)
        debug = blocks.message_debug()
        self.tb.msg_connect(dut, "out", debug, "store")
        pdu = pmt.cons(pmt.PMT_NIL, pmt.init_u8vector(len(payload), list(payload)))

        self.tb.start()
        dut.to_basic_block()._post(pmt.intern("to_nmea"), pdu)
        deadline = time.monotonic() + 5.0
        while debug.num_messages() < 1 and time.monotonic() < deadline:
            time.sleep(0.01)
        self.tb.stop()
        self.tb.wait()

        self.assertEqual(debug.num_messages(), 1)
        return bytes(pmt.u8vector_elements(pmt.cdr(debug.get_message(0)))).decode()

    def test_encodes_reference_position_report(self):
        sentence = self._to_nmea("B", REFERENCE_PAYLOAD)

        self.assertEqual(sentence, REFERENCE_SENTENCE)

    def test_fragments_and_pads_long_payload(self):
        sentences = self._to_nmea("A", PADDED_TWO_FRAGMENT_PAYLOAD)

        self.assertEqual(sentences, PADDED_TWO_FRAGMENT_SENTENCES)


if __name__ == '__main__':
    gr_unittest.run(qa_pdu_to_nmea)
