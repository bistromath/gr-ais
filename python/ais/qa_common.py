#
# Copyright 2026 Nick Foster
#
# SPDX-License-Identifier: GPL-3.0-or-later
#

# Shared helpers for the QA tests: AIS/HDLC frame construction and GMSK
# modulation of unpacked bits. Not installed.

import numpy as np
from gnuradio import gr, blocks, digital

SAMP_RATE = 48000.0
BIT_RATE = 9600.0
SPS = int(SAMP_RATE / BIT_RATE)
GMSK_BT = 0.4

# 21-byte payload of the published reference position report
# !AIVDM,1,1,,B,177KQJ5000G?tO`K>RA1wUbN0TKH,0*5C
REFERENCE_PAYLOAD = bytes([4, 113, 219, 133, 161, 64, 0, 5, 207, 241, 250,
                           27, 58, 36, 65, 254, 90, 158, 2, 70, 216])
REFERENCE_SENTENCE = "!AIVDM,1,1,,B,177KQJ5000G?tO`K>RA1wUbN0TKH,0*5C"


def crc16_x25(data):
    crc = 0xffff
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = (crc >> 1) ^ 0x8408 if crc & 1 else crc >> 1
    return crc ^ 0xffff


def bits_lsb_first(data):
    return [(byte >> i) & 1 for byte in data for i in range(8)]


def stuff_bits(bits):
    out = []
    ones = 0
    for bit in bits:
        out.append(bit)
        ones = ones + 1 if bit else 0
        if ones == 5:
            out.append(0)
            ones = 0
    return out


def nrzi_encode(bits):
    out = []
    level = 1
    for bit in bits:
        if bit == 0:
            level ^= 1
        out.append(level)
    return out


def hdlc_frame(payload):
    crc = crc16_x25(payload)
    flag = [0, 1, 1, 1, 1, 1, 1, 0]
    body = stuff_bits(bits_lsb_first(payload + bytes([crc & 0xff, crc >> 8])))
    return flag + body + flag


def gmsk_modulate(bits):
    padded = bits + [0] * ((8 - len(bits) % 8) % 8)
    packed = np.packbits(np.array(padded, dtype=np.uint8))

    tb = gr.top_block()
    src = blocks.vector_source_b(packed.tolist())
    mod = digital.gmsk_mod(SPS, GMSK_BT)
    sink = blocks.vector_sink_c()
    tb.connect(src, mod, sink)
    tb.run()

    return np.array(sink.data())
