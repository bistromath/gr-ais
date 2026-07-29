/* -*- c++ -*- */
/*
 * Copyright 2026 Nick Foster.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef INCLUDED_AIS_VITERBI_CPM_CB_IMPL_H
#define INCLUDED_AIS_VITERBI_CPM_CB_IMPL_H

#include <gnuradio/ais/viterbi_cpm_cb.h>
#include <array>

namespace gr {
  namespace ais {

    class viterbi_cpm_cb_impl : public viterbi_cpm_cb
    {
     private:
      std::vector<gr_complex> d_branch_signals;
      std::vector<int> d_next_state;
      std::vector<std::array<int, 2>> d_prev_branch;
      int d_num_states;
      int d_sps;
      int d_traceback_len;
      float d_phase_gain;

      float d_phase;
      std::vector<float> d_metrics;
      std::vector<float> d_new_metrics;
      std::vector<int> d_survivors;
      std::vector<gr_complex> d_rotated;
      std::vector<float> d_branch_metrics;
      int d_ring_pos;
      long d_symbols_seen;

      char process_symbol(const gr_complex *in);

     public:
      viterbi_cpm_cb_impl(const std::vector<gr_complex> &branch_signals, const std::vector<int> &next_state, int samples_per_symbol, int traceback_len, float phase_gain);
      ~viterbi_cpm_cb_impl();

      void forecast (int noutput_items, gr_vector_int &ninput_items_required);

      int general_work(int noutput_items,
           gr_vector_int &ninput_items,
           gr_vector_const_void_star &input_items,
           gr_vector_void_star &output_items);

    };

  } // namespace ais
} // namespace gr

#endif /* INCLUDED_AIS_VITERBI_CPM_CB_IMPL_H */
