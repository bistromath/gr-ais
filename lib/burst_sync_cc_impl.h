/* -*- c++ -*- */
/*
 * Copyright 2026 Nick Foster.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef INCLUDED_AIS_BURST_SYNC_CC_IMPL_H
#define INCLUDED_AIS_BURST_SYNC_CC_IMPL_H

#include <gnuradio/ais/burst_sync_cc.h>
#include <deque>

namespace gr {
  namespace ais {

    class burst_sync_cc_impl : public burst_sync_cc
    {
     private:
      struct candidate {
          float score;
          int64_t lag;
          size_t hyp;
          bool variant_b;
      };
      struct correction {
          uint64_t start;   // output item where the new rotation begins
          float freq;       // rad/sample
          float phase_est;
          int seed_state;
      };
      struct pending_tag {
          uint64_t offset;
          float phase;
          int state;
      };

      std::vector<std::vector<gr_complex>> d_refs;  // conjugated taps, [2*hyp + variant]
      std::vector<float> d_freqs;                   // rad/sample per hypothesis
      float d_threshold;
      int d_tag_offset;
      int d_seed_state_a;
      int d_seed_state_b;
      int d_ref_len;
      int d_margin;
      int d_delay;
      int d_holdoff;

      int64_t d_next_lag;
      bool d_searching;
      candidate d_best;
      std::deque<correction> d_pending;
      std::deque<pending_tag> d_tags;
      float d_freq;         // active rotation, rad/sample
      float d_phase;        // rotation phase at the next output item
      std::vector<double> d_energy;

      void evaluate_lags(const gr_complex *buf, int64_t buf_start, int64_t lag_end);
      void confirm(const gr_complex *buf, int64_t buf_start);

     public:
      burst_sync_cc_impl(const std::vector<gr_complex> &reference, double sample_rate, const std::vector<float> &freq_hypotheses, float threshold, int tag_offset, int seed_state_a, int seed_state_b, int holdoff);
      ~burst_sync_cc_impl();

      int work(
              int noutput_items,
              gr_vector_const_void_star &input_items,
              gr_vector_void_star &output_items
      );
    };

  } // namespace ais
} // namespace gr

#endif /* INCLUDED_AIS_BURST_SYNC_CC_IMPL_H */
