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


def gmsk_trellis(samples_per_symbol, bt=0.4):
    '''Returns (branch_signals, next_state, num_states) for viterbi_cpm_cb.

    Branch index b corresponds to source state b//2 and input bit b&1;
    branch_signals is flattened [num_branches][samples_per_symbol].'''
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
