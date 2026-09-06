/* -*- c++ -*- */
/*
 * Copyright 2026 Nick Foster.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <gnuradio/io_signature.h>
#include <gnuradio/expj.h>
#include <volk/volk.h>
#include <cmath>
#include <stdexcept>
#include "burst_sync_cc_impl.h"

namespace gr {
  namespace ais {

    static const pmt::pmt_t PHASE_KEY = pmt::mp("burst_phase");
    static const pmt::pmt_t STATE_KEY = pmt::mp("burst_state");

    burst_sync_cc::sptr
    burst_sync_cc::make(const std::vector<gr_complex> &reference, double sample_rate, const std::vector<float> &freq_hypotheses, float threshold, int tag_offset, int seed_state_a, int seed_state_b, int holdoff)
    {
      return gnuradio::make_block_sptr<burst_sync_cc_impl>(
        reference, sample_rate, freq_hypotheses, threshold, tag_offset, seed_state_a, seed_state_b, holdoff);
    }

    burst_sync_cc_impl::burst_sync_cc_impl(const std::vector<gr_complex> &reference, double sample_rate, const std::vector<float> &freq_hypotheses, float threshold, int tag_offset, int seed_state_a, int seed_state_b, int holdoff)
      : gr::sync_block("burst_sync_cc",
              gr::io_signature::make(1, 1, sizeof(gr_complex)),
              gr::io_signature::make(1, 1, sizeof(gr_complex))),
        d_threshold(threshold),
        d_tag_offset(tag_offset),
        d_seed_state_a(seed_state_a),
        d_seed_state_b(seed_state_b),
        d_ref_len(reference.size()),
        // the preamble's periodicity puts correlation shoulders up to a
        // full reference length before the true peak
        d_margin(reference.size()),
        d_delay(d_ref_len + d_margin),
        d_holdoff(holdoff),
        d_next_lag(0),
        d_searching(false),
        d_freq(0.0f),
        d_phase(0.0f)
    {
        if (reference.empty())
            throw std::invalid_argument("reference must not be empty");
        if (freq_hypotheses.empty())
            throw std::invalid_argument("at least one frequency hypothesis is required");
        if (tag_offset < 0 || tag_offset >= d_ref_len)
            throw std::invalid_argument("tag_offset must lie within the reference");
        if (holdoff < d_ref_len)
            throw std::invalid_argument("holdoff must be at least the reference length");

        float energy = 0.0f;
        for (const auto &s : reference)
            energy += std::norm(s);
        const float scale = 1.0f / std::sqrt(energy);

        // per hypothesis, the conjugated correlation taps for both NRZI
        // variants; variant B's waveform is the conjugate of variant A's
        for (float f_hz : freq_hypotheses) {
            const float w = 2.0f * float(M_PI) * f_hz / float(sample_rate);
            std::vector<gr_complex> taps_a(d_ref_len), taps_b(d_ref_len);
            for (int i = 0; i < d_ref_len; i++) {
                const gr_complex r = reference[i] * scale;
                taps_a[i] = std::conj(r * gr_expj(w * i));
                taps_b[i] = r * gr_expj(-w * i);
            }
            d_refs.push_back(std::move(taps_a));
            d_refs.push_back(std::move(taps_b));
            d_freqs.push_back(w);
        }

        set_history(d_delay + 1);
    }

    burst_sync_cc_impl::~burst_sync_cc_impl()
    {
    }

    void
    burst_sync_cc_impl::evaluate_lags(const gr_complex *buf, int64_t buf_start, int64_t lag_end)
    {
        for (int64_t lag = std::max<int64_t>(d_next_lag, buf_start); lag < lag_end; lag++) {
            if (d_searching && lag > d_best.lag + d_margin) {
                confirm(buf, buf_start);
                lag = std::max(lag, d_next_lag) - 1;   // skip the holdoff span
                continue;
            }
            const int rel = lag - buf_start;
            const double energy = d_energy[rel + d_ref_len] - d_energy[rel];
            if (energy <= 0.0f)
                continue;
            for (size_t r = 0; r < d_refs.size(); r++) {
                gr_complex c;
                volk_32fc_x2_dot_prod_32fc(&c, &buf[rel], d_refs[r].data(), d_ref_len);
                const float score = std::norm(c) / energy;
                if (score <= d_threshold)
                    continue;
                if (!d_searching || score > d_best.score) {
                    d_best = {score, lag, r / 2, (r & 1) != 0};
                    d_searching = true;
                }
            }
        }
        d_next_lag = std::max<int64_t>(d_next_lag, lag_end);
    }

    void
    burst_sync_cc_impl::confirm(const gr_complex *buf, int64_t buf_start)
    {
        const int rel = d_best.lag - buf_start;
        const gr_complex *taps = d_refs[d_best.hyp * 2 + (d_best.variant_b ? 1 : 0)].data();
        const int half = d_ref_len / 2;

        gr_complex c1, c2;
        volk_32fc_x2_dot_prod_32fc(&c1, &buf[rel], taps, half);
        volk_32fc_x2_dot_prod_32fc(&c2, &buf[rel + half], &taps[half], d_ref_len - half);
        const float w_fine = d_freqs[d_best.hyp] + std::arg(c2 * std::conj(c1)) / half;

        // the taps already carry the hypothesis rotation, so only the fine
        // part remains to be wiped for the phase estimate
        const float w_delta = w_fine - d_freqs[d_best.hyp];
        gr_complex acc(0, 0);
        for (int i = 0; i < d_ref_len; i++)
            acc += buf[rel + i] * taps[i] * gr_expj(-w_delta * i);

        correction corr;
        corr.start = d_best.lag + d_delay;
        corr.freq = w_fine;
        corr.phase_est = std::arg(acc);
        corr.seed_state = d_best.variant_b ? d_seed_state_b : d_seed_state_a;
        d_pending.push_back(corr);

        d_next_lag = d_best.lag + d_holdoff;
        d_searching = false;
    }

    int
    burst_sync_cc_impl::work (int noutput_items,
                       gr_vector_const_void_star &input_items,
                       gr_vector_void_star &output_items)
    {
      auto in = static_cast<const gr_complex*>(input_items[0]);
      auto out = static_cast<gr_complex*>(output_items[0]);

      // at stream start the history region holds pre-stream zeros, so the
      // buffer origin can be negative
      const int64_t buf_start = (int64_t)nitems_read(0) - d_delay;
      const int navail = noutput_items + d_delay;

      d_energy.resize(navail + 1);
      // Preserve weak windows when subtracting prefix sums after a strong burst.
      d_energy[0] = 0.0;
      for (int i = 0; i < navail; i++)
          d_energy[i + 1] = d_energy[i] + std::norm(in[i]);

      evaluate_lags(in, buf_start, buf_start + navail - d_ref_len + 1);

      const uint64_t out_base = nitems_written(0);
      for (int i = 0; i < noutput_items; i++) {
          const uint64_t o = out_base + i;
          while (!d_pending.empty() && d_pending.front().start <= o) {
              const correction &c = d_pending.front();
              d_freq = c.freq;
              d_phase = -c.freq * float(o - c.start);
              d_tags.push_back({c.start + d_tag_offset, c.phase_est, c.seed_state});
              d_pending.pop_front();
          }
          out[i] = in[i] * gr_expj(d_phase);
          d_phase -= d_freq;
          d_phase = std::fmod(d_phase, 2.0f * float(M_PI));
      }

      while (!d_tags.empty() && d_tags.front().offset < out_base + noutput_items) {
          const pending_tag &t = d_tags.front();
          add_item_tag(0, t.offset, PHASE_KEY, pmt::from_double(t.phase), alias_pmt());
          add_item_tag(0, t.offset, STATE_KEY, pmt::from_long(t.state), alias_pmt());
          d_tags.pop_front();
      }

      return noutput_items;
    }

  } /* namespace ais */
} /* namespace gr */
