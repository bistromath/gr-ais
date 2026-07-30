/* -*- c++ -*- */
/*
 * Copyright 2026 Nick Foster.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef INCLUDED_AIS_BURST_SYNC_CC_H
#define INCLUDED_AIS_BURST_SYNC_CC_H

#include <gnuradio/ais/api.h>
#include <gnuradio/sync_block.h>

namespace gr {
  namespace ais {

    /*!
     * \brief Data-aided burst synchronizer for AIS GMSK.
     * \ingroup ais
     *
     * Correlates the input against a known modulated preamble-plus-flag
     * reference (from gnuradio.ais.cpm_trellis.gmsk_burst_reference()) at a
     * bank of coarse frequency hypotheses, for both NRZI level variants
     * (the complement of an NRZI sequence conjugates the waveform). On a
     * detection it refines the frequency with a split-correlation phase
     * slope, derotates the output from the burst onward, and emits two
     * stream tags at the seeding point one symbol before the HDLC flag:
     * "burst_phase" (carrier phase of the derotated signal against the
     * reference, double) and "burst_state" (known trellis state for the
     * detected NRZI variant, long), which viterbi_cpm_cb uses to realign,
     * reset, and seed its trellis.
     *
     * Output is the input delayed by the reference length plus the peak
     * search margin, so corrections and tags always land before the flag
     * reaches the decoder. Detection compares the energy-normalized
     * squared correlation against \p threshold (0..1).
     */
    class AIS_API burst_sync_cc : virtual public gr::sync_block
    {
     public:
      typedef std::shared_ptr<burst_sync_cc> sptr;

      /*!
       * \brief Make an AIS burst synchronizer.
       *
       * \param reference: Modulated preamble+flag waveform, NRZI variant A.
       * \param sample_rate: Input sample rate in Hz.
       * \param freq_hypotheses: Coarse frequency offsets to search, Hz.
       * \param threshold: Normalized squared-correlation detection
       *                   threshold (0..1). GMSK correlates substantially
       *                   against any GMSK data, so this must sit above the
       *                   data-vs-reference sidelobe level; try 0.4.
       * \param tag_offset: Samples from the correlation peak to the
       *                    seeding point.
       * \param seed_state_a: Trellis state at the seeding point, variant A.
       * \param seed_state_b: Trellis state at the seeding point, variant B.
       * \param holdoff: Samples to suppress further detections after a
       *                 burst; span at least the whole burst.
       */
      static sptr make(const std::vector<gr_complex> &reference, double sample_rate, const std::vector<float> &freq_hypotheses, float threshold, int tag_offset, int seed_state_a, int seed_state_b, int holdoff);
    };

  } // namespace ais
} // namespace gr

#endif /* INCLUDED_AIS_BURST_SYNC_CC_H */
