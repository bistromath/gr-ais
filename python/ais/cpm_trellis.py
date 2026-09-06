#
# Copyright 2026 Nick Foster
#
# SPDX-License-Identifier: GPL-3.0-or-later
#

# Trellis description of GMSK for the viterbi_cpm_cb demodulator.
#
# GMSK with h=1/2 is a finite-state machine over the carrier phase: each
# fully-elapsed symbol advances the phase by +-pi/2, so the cumulative
# phase takes one of four values, and the L-1 most recent symbols still
# shaping the waveform complete the state. Branch signals are synthesized
# from the same Gaussian frequency pulse gr's gmsk_mod uses, so the
# trellis matches the transmitted waveform exactly.

import numpy as np
from gnuradio import filter as grfilter

RAMP_SYMBOLS = 8
PREAMBLE_SYMBOLS = 24
FLAG_BITS = [0, 1, 1, 1, 1, 1, 1, 0]
REFERENCE_START_SYMBOL = 16
REFERENCE_SYMBOLS = 24
SLOT_SYMBOLS = 256
DEFAULT_TRACEBACK_LEN = 48
DEFAULT_PHASE_GAIN = 0.1


def gmsk_trellis(samples_per_symbol, bt=0.4):
    '''Returns (branch_signals, next_state, num_states) for viterbi_cpm_cb.

    Branch index b corresponds to source state b//2 and input bit b&1;
    branch_signals is flattened [num_branches][samples_per_symbol].
    State = phase_index * 2**(memory-1) + history: phase_index selects a
    multiple of pi/2, and history holds the last memory-1 input bits,
    oldest first (most significant bit).'''
    sps = samples_per_symbol
    gauss = grfilter.firdes.gaussian(1, sps, bt, 4 * sps)
    taps = np.convolve(gauss, np.ones(sps))
    memory = int(np.ceil(len(taps) / sps))
    sensitivity = (np.pi / 2) / sps
    cum = np.concatenate([np.cumsum(taps), np.full(memory * sps, taps.sum())])

    history_states = 2 ** (memory - 1)
    num_states = 4 * history_states
    signals = np.zeros((num_states * 2, sps), dtype=np.complex64)
    next_state = np.zeros(num_states * 2, dtype=int)
    for phase_idx in range(4):
        for hist in range(history_states):
            history = [(hist >> (memory - 2 - m)) & 1 for m in range(memory - 1)]
            for bit in (0, 1):
                symbols = history + [bit]
                phase = np.full(sps, phase_idx * np.pi / 2)
                for age in range(memory):
                    nrz = 2 * symbols[memory - 1 - age] - 1
                    phase += sensitivity * nrz * cum[age * sps + np.arange(sps)]
                branch = (phase_idx * history_states + hist) * 2 + bit
                signals[branch] = np.exp(1j * phase)
                next_phase_idx = (phase_idx + (2 * history[0] - 1)) % 4
                next_hist = 0
                for s in history[1:] + [bit]:
                    next_hist = next_hist * 2 + s
                next_state[branch] = next_phase_idx * history_states + next_hist
    return (signals.flatten().tolist(), next_state.tolist(), num_states)


def _nrzi(bits):
    out = []
    level = 1
    for bit in bits:
        if bit == 0:
            level ^= 1
        out.append(level)
    return out


def _chain(signals, next_state, num_states, sps, levels, start, nsym):
    table = np.array(signals).reshape(num_states * 2, sps)
    history_bits = (num_states // 4).bit_length() - 1
    state = 0
    for lvl in levels[start - history_bits:start]:
        state = state * 2 + lvl
    out = []
    for lvl in levels[start:start + nsym]:
        branch = state * 2 + lvl
        out.append(table[branch])
        state = next_state[branch]
    return np.concatenate(out), state


def gmsk_burst_reference(samples_per_symbol, bt=0.4):
    '''Returns (reference, tag_offset, seed_state_a, seed_state_b) for
    burst_sync_cc: the modulated NRZI waveform of the last 16 preamble
    symbols plus the HDLC start flag (variant A, initial NRZI level 1;
    variant B is its complex conjugate), the offset in samples from the
    correlation peak to the seeding point one symbol before the flag, and
    the known trellis state there for each NRZI variant.'''
    signals, next_state, num_states = gmsk_trellis(samples_per_symbol, bt)
    prefix = [0, 1] * ((RAMP_SYMBOLS + PREAMBLE_SYMBOLS) // 2) + FLAG_BITS
    levels_a = _nrzi(prefix)
    levels_b = [1 - lvl for lvl in levels_a]
    reference, _ = _chain(signals, next_state, num_states, samples_per_symbol,
                          levels_a, REFERENCE_START_SYMBOL, REFERENCE_SYMBOLS)
    tag_symbol = RAMP_SYMBOLS + PREAMBLE_SYMBOLS - 1
    _, seed_a = _chain(signals, next_state, num_states, samples_per_symbol,
                       levels_a, REFERENCE_START_SYMBOL, tag_symbol - REFERENCE_START_SYMBOL)
    _, seed_b = _chain(signals, next_state, num_states, samples_per_symbol,
                       levels_b, REFERENCE_START_SYMBOL, tag_symbol - REFERENCE_START_SYMBOL)
    tag_offset = (tag_symbol - REFERENCE_START_SYMBOL) * samples_per_symbol
    return (reference.tolist(), tag_offset, seed_a, seed_b)
