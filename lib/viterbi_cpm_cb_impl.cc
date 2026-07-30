/* -*- c++ -*- */
/*
 * Copyright 2026 Nick Foster.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <gnuradio/io_signature.h>
#include <gnuradio/expj.h>
#include <cmath>
#include <stdexcept>
#include "viterbi_cpm_cb_impl.h"

namespace gr {
  namespace ais {

    static const pmt::pmt_t PHASE_KEY = pmt::mp("burst_phase");
    static const pmt::pmt_t STATE_KEY = pmt::mp("burst_state");

    viterbi_cpm_cb::sptr
    viterbi_cpm_cb::make(const std::vector<gr_complex> &branch_signals, const std::vector<int> &next_state, int samples_per_symbol, int traceback_len, float phase_gain)
    {
      return gnuradio::make_block_sptr<viterbi_cpm_cb_impl>(
        branch_signals, next_state, samples_per_symbol, traceback_len, phase_gain);
    }

    viterbi_cpm_cb_impl::viterbi_cpm_cb_impl(const std::vector<gr_complex> &branch_signals, const std::vector<int> &next_state, int samples_per_symbol, int traceback_len, float phase_gain)
      : gr::block("viterbi_cpm_cb",
              gr::io_signature::make(1, 1, sizeof(gr_complex)),
              gr::io_signature::make(1, 1, sizeof(char))),
        d_branch_signals(branch_signals),
        d_num_states(next_state.size() / 2),
        d_sps(samples_per_symbol),
        d_traceback_len(traceback_len),
        d_phase_gain(phase_gain),
        d_phase(0.0f),
        d_metrics(d_num_states, 0.0f),
        d_new_metrics(d_num_states, 0.0f),
        d_survivors(traceback_len * d_num_states, 0),
        d_rotated(samples_per_symbol),
        d_branch_metrics(next_state.size()),
        d_ring_pos(0),
        d_symbols_seen(0)
    {
        if (samples_per_symbol < 1)
            throw std::invalid_argument("samples_per_symbol must be positive");
        if (next_state.size() < 2 || next_state.size() % 2 != 0)
            throw std::invalid_argument("next_state must hold two branches per state");
        if (branch_signals.size() != next_state.size() * (size_t)samples_per_symbol)
            throw std::invalid_argument("branch_signals size must be num_branches * samples_per_symbol");
        if (traceback_len < 1)
            throw std::invalid_argument("traceback_len must be positive");

        d_prev_branch.assign(d_num_states, {-1, -1});
        for (size_t b = 0; b < next_state.size(); b++) {
            int dest = next_state[b];
            if (dest < 0 || dest >= d_num_states)
                throw std::invalid_argument("next_state entry out of range");
            if (d_prev_branch[dest][0] < 0)
                d_prev_branch[dest][0] = b;
            else if (d_prev_branch[dest][1] < 0)
                d_prev_branch[dest][1] = b;
            else
                throw std::invalid_argument("trellis must have exactly two branches into each state");
        }
        for (const auto &pb : d_prev_branch) {
            if (pb[1] < 0)
                throw std::invalid_argument("trellis must have exactly two branches into each state");
        }

        set_relative_rate(1, (uint64_t)samples_per_symbol);
        set_output_multiple(1);
    }

    viterbi_cpm_cb_impl::~viterbi_cpm_cb_impl()
    {
    }

    char
    viterbi_cpm_cb_impl::process_symbol(const gr_complex *in)
    {
        const int num_branches = 2 * d_num_states;
        const gr_complex rot = gr_expj(-d_phase);

        gr_complex *rk = d_rotated.data();
        for (int i = 0; i < d_sps; i++)
            rk[i] = in[i] * rot;

        float *bm = d_branch_metrics.data();
        for (int b = 0; b < num_branches; b++) {
            gr_complex corr(0, 0);
            const gr_complex *sig = &d_branch_signals[b * d_sps];
            for (int i = 0; i < d_sps; i++)
                corr += std::conj(sig[i]) * rk[i];
            bm[b] = corr.real();
        }

        int *surv = &d_survivors[d_ring_pos * d_num_states];
        for (int s = 0; s < d_num_states; s++) {
            const int b0 = d_prev_branch[s][0];
            const int b1 = d_prev_branch[s][1];
            const float m0 = d_metrics[b0 / 2] + bm[b0];
            const float m1 = d_metrics[b1 / 2] + bm[b1];
            if (m0 >= m1) {
                d_new_metrics[s] = m0;
                surv[s] = b0;
            } else {
                d_new_metrics[s] = m1;
                surv[s] = b1;
            }
        }

        int best_state = 0;
        for (int s = 1; s < d_num_states; s++)
            if (d_new_metrics[s] > d_new_metrics[best_state])
                best_state = s;
        const float best_metric = d_new_metrics[best_state];
        for (int s = 0; s < d_num_states; s++)
            d_metrics[s] = d_new_metrics[s] - best_metric;

        if (d_phase_gain != 0.0f) {
            const gr_complex *sig = &d_branch_signals[surv[best_state] * d_sps];
            gr_complex corr(0, 0);
            for (int i = 0; i < d_sps; i++)
                corr += std::conj(sig[i]) * rk[i];
            d_phase += d_phase_gain * std::arg(corr);
            d_phase = std::fmod(d_phase, 2.0f * float(M_PI));
        }

        d_symbols_seen++;
        d_ring_pos = (d_ring_pos + 1) % d_traceback_len;

        if (d_symbols_seen < d_traceback_len)
            return -1;

        int s = best_state;
        int b = 0;
        for (int i = 1; i <= d_traceback_len; i++) {
            const int pos = (d_ring_pos - i + d_traceback_len) % d_traceback_len;
            b = d_survivors[pos * d_num_states + s];
            s = b / 2;
        }
        return b & 1;
    }

    void
    viterbi_cpm_cb_impl::seed(int state, float phase)
    {
        std::fill(d_metrics.begin(), d_metrics.end(), -1e30f);
        if (state >= 0 && state < d_num_states)
            d_metrics[state] = 0.0f;
        d_phase = phase;
        d_symbols_seen = 0;
        d_ring_pos = 0;
    }

    void
    viterbi_cpm_cb_impl::forecast (int noutput_items, gr_vector_int &ninput_items_required)
    {
        ninput_items_required[0] = noutput_items * d_sps;
    }

    int
    viterbi_cpm_cb_impl::general_work (int noutput_items,
                       gr_vector_int &ninput_items,
                       gr_vector_const_void_star &input_items,
                       gr_vector_void_star &output_items)
    {
      auto in = static_cast<const gr_complex*>(input_items[0]);
      auto out = static_cast<char*>(output_items[0]);

      const uint64_t start = nitems_read(0);
      const uint64_t end = start + ninput_items[0];

      std::vector<tag_t> state_tags, phase_tags;
      get_tags_in_range(state_tags, 0, start, end, STATE_KEY);
      get_tags_in_range(phase_tags, 0, start, end, PHASE_KEY);

      int consumed = 0;
      int produced = 0;
      size_t ti = 0;
      while (true) {
          const uint64_t pos = start + consumed;
          if (ti < state_tags.size() && state_tags[ti].offset <= pos) {
              float phase = 0.0f;
              for (const auto &pt : phase_tags)
                  if (pt.offset == state_tags[ti].offset)
                      phase = pmt::to_double(pt.value);
              seed(pmt::to_long(state_tags[ti].value), phase);
              ti++;
              continue;
          }
          // a tag mid-symbol redefines the symbol grid: discard the partial
          // symbol and realign to the tagged sample
          const uint64_t limit = ti < state_tags.size()
                                 ? std::min(end, state_tags[ti].offset) : end;
          if (pos + d_sps <= limit) {
              if (produced >= noutput_items)
                  break;
              const char bit = process_symbol(&in[consumed]);
              consumed += d_sps;
              if (bit >= 0)
                  out[produced++] = bit;
          } else if (ti < state_tags.size() && limit == state_tags[ti].offset) {
              consumed = limit - start;
          } else {
              break;
          }
      }

      consume_each(consumed);
      return produced;
    }

  } /* namespace ais */
} /* namespace gr */
