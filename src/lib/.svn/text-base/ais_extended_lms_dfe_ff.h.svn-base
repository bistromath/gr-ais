#ifndef INCLUDED_AIS_EXTENDED_LMS_DFE_FF_H
#define INCLUDED_AIS_EXTENDED_LMS_DFE_FF_H

#include <gr_sync_block.h>

class ais_extended_lms_dfe_ff;
typedef boost::shared_ptr<ais_extended_lms_dfe_ff> ais_extended_lms_dfe_ff_sptr;

ais_extended_lms_dfe_ff_sptr ais_make_extended_lms_dfe_ff (float lambda_ff, float lambda_fb,
				       unsigned int num_fftaps, unsigned int num_fbtaps);

/*!
 * \brief Least-Mean-Square Decision Feedback Equalizer (float in/out) with reset input and preamble training
 * \ingroup eq_blk
 */
class ais_extended_lms_dfe_ff : public gr_sync_block
{
  friend ais_extended_lms_dfe_ff_sptr ais_make_extended_lms_dfe_ff (float lambda_ff, float lambda_fb,
						unsigned int num_fftaps, unsigned int num_fbtaps);
  
  float	d_lambda_ff;
  float	d_lambda_fb;
  std::vector<float>  d_ff_delayline;
  std::vector<float>  d_fb_delayline;
  std::vector<float>  d_ff_taps;
  std::vector<float>  d_fb_taps;
  unsigned int d_ff_index;
  unsigned int d_fb_index;
  unsigned int d_resetcounter;

  ais_extended_lms_dfe_ff (float lambda_ff, float lambda_fb, 
		 unsigned int num_fftaps, unsigned int num_fbtaps);

  void reset(void);  

  public:
  
  int work (int noutput_items,
	    gr_vector_const_void_star &input_items,
	    gr_vector_void_star &output_items);
};

#endif
