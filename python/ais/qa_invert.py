#!/usr/bin/env python
#
# Copyright 2026 Nick Foster
#
# SPDX-License-Identifier: GPL-3.0-or-later
#

from gnuradio import gr, gr_unittest, blocks
from gnuradio import ais


class qa_invert(gr_unittest.TestCase):

    def setUp(self):
        self.tb = gr.top_block()

    def tearDown(self):
        self.tb = None

    def test_inverts_lsb_and_masks_high_bits(self):
        src = blocks.vector_source_b([0, 1, 1, 0, 0xff, 0x02])
        dut = ais.invert()
        sink = blocks.vector_sink_b()
        self.tb.connect(src, dut, sink)

        self.tb.run()

        self.assertEqual(list(sink.data()), [1, 0, 0, 1, 0, 1])


if __name__ == '__main__':
    gr_unittest.run(qa_invert)
