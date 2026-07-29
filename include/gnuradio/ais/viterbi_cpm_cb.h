/* -*- c++ -*- */
/*
 * Copyright 2026 Nick Foster.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef INCLUDED_AIS_VITERBI_CPM_CB_H
#define INCLUDED_AIS_VITERBI_CPM_CB_H

#include <gnuradio/ais/api.h>
#include <gnuradio/block.h>

namespace gr {
  namespace ais {

    /*!
     * \brief Coherent MLSE demodulator for binary CPM (e.g. GMSK).
     * \ingroup ais
     *
     * Runs a streaming Viterbi algorithm over a CPM trellis described by a
     * branch-signal table and a next-state table, as produced by
     * gnuradio.ais.cpm_trellis.gmsk_trellis(). The branch metric is the
     * real part of the correlation of the received symbol interval with
     * the branch signal, the maximum-likelihood metric for equal-energy
     * CPM signals in AWGN.
     *
     * The cumulative carrier phase is part of the trellis state and the
     * decoded bits are its increments, so a constant carrier phase offset
     * is absorbed by the phase states up to a residual of at most a
     * quarter turn; a first-order decision-directed loop driven by the
     * best survivor's branch tracks that residual and slow phase drift.
     * The input must be symbol-synchronized at samples_per_symbol samples
     * per symbol (e.g. from a Symbol Sync block with osps=2).
     *
     * Output is one unpacked bit per symbol, delayed by traceback_len
     * symbols.
     */
    class AIS_API viterbi_cpm_cb : virtual public gr::block
    {
     public:
      typedef std::shared_ptr<viterbi_cpm_cb> sptr;

      /*!
       * \brief Make a CPM Viterbi demodulator.
       *
       * \param branch_signals: Complex branch waveforms, flattened
       *                        [num_branches][samples_per_symbol]. Branch
       *                        index b corresponds to source state b/2 and
       *                        input bit b&1.
       * \param next_state: Destination state per branch, length num_branches.
       * \param samples_per_symbol: Samples per symbol of the input stream.
       * \param traceback_len: Survivor traceback depth in symbols.
       * \param phase_gain: Gain of the decision-directed phase tracking
       *                    loop, in radians per radian of detected error.
       *                    0 disables tracking.
       */
      static sptr make(const std::vector<gr_complex> &branch_signals, const std::vector<int> &next_state, int samples_per_symbol, int traceback_len, float phase_gain);
    };

  } // namespace ais
} // namespace gr

#endif /* INCLUDED_AIS_VITERBI_CPM_CB_H */
