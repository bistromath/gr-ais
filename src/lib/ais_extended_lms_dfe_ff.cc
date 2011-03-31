#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "ais_extended_lms_dfe_ff.h"
#include <gr_io_signature.h>
#include <gr_misc.h>
#include <iostream>

float
slice(float val)
{
  if (val>0)
    return 1;
  else
    return -1;
}

ais_extended_lms_dfe_ff_sptr
ais_make_extended_lms_dfe_ff (float lambda_ff, float lambda_fb, 
		    unsigned int num_fftaps, unsigned int num_fbtaps)
{
  return ais_extended_lms_dfe_ff_sptr (new ais_extended_lms_dfe_ff (lambda_ff,lambda_fb,num_fftaps,num_fbtaps));
}

ais_extended_lms_dfe_ff::ais_extended_lms_dfe_ff (float lambda_ff, float lambda_fb , 
			      unsigned int num_fftaps, unsigned int num_fbtaps)
  : gr_sync_block ("ais_extended_lms_dfe_ff",
		   gr_make_io_signature2 (2, 2, sizeof (float), sizeof(char)),
		   gr_make_io_signature (1, 1, sizeof (float))),
    d_lambda_ff (lambda_ff), d_lambda_fb (lambda_fb), 
    d_ff_delayline(gr_rounduppow2(num_fftaps)),
    d_fb_delayline(gr_rounduppow2(num_fbtaps)),
    d_ff_taps(num_fftaps), d_fb_taps(num_fbtaps),
    d_ff_index(0), d_fb_index(0), d_resetcounter(0)
{
  gr_zero_vector(d_ff_taps);
  d_ff_taps [d_ff_taps.size()/2] = 1;

  gr_zero_vector(d_fb_taps);
  gr_zero_vector(d_ff_delayline);
  gr_zero_vector(d_fb_delayline);

  set_output_multiple(300);
}

void ais_extended_lms_dfe_ff::reset(void)
{
//	d_ff_index = 0;
//	d_fb_index = 0;

	gr_zero_vector(d_ff_taps);
	d_ff_taps[d_ff_taps.size()/2] = 1;

	gr_zero_vector(d_fb_taps);
	gr_zero_vector(d_ff_delayline);
	gr_zero_vector(d_fb_delayline);

	d_ff_index = 0;
	d_fb_index = 0;

	d_resetcounter = 0;
}

int
ais_extended_lms_dfe_ff::work (int noutput_items,
		   gr_vector_const_void_star &input_items,
		   gr_vector_void_star &output_items)
{
  const float *iptr = (const float *) input_items[0];
  const char *i2ptr = (const char *) input_items[1];

  float *optr = (float *) output_items[0];
  
  float acc, decision, error;
  unsigned int i;

  unsigned int ff_mask = d_ff_delayline.size() - 1;	// size is power of 2
  unsigned int fb_mask = d_fb_delayline.size() - 1;

  int	size = noutput_items;
  while(size-- > 0) {

	if((*i2ptr++ & 0x02) && (d_resetcounter > 50)) {
		if(size < 150) {
			//printf("Returning, not enough to work with\n");
			return noutput_items-size; //don't have enough to work with for an entire packet so wait for the next one
		}
		reset(); //if the correlator bit (bit 1/LSB-1) is set, reset taps to 0
		
		//now we train the DFE by looping through the first 150 bits a dozen times or so.

		for(int k = 0; k < 12; k++) { //train 12 times
			const float *iptr_train = iptr; //a temporary input pointer to train with
			while((iptr_train - iptr) < 150) {
			    acc = 0; 
			    d_ff_delayline[d_ff_index] = *iptr_train++;

			    // Compute output
			    for (i=0; i < d_ff_taps.size(); i++) 
			      acc += d_ff_delayline[(i+d_ff_index) & ff_mask] * d_ff_taps[i];
    
			    for (i=0; i < d_fb_taps.size(); i++)
			      acc -= d_fb_delayline[(i+d_fb_index) & fb_mask] * d_fb_taps[i];

			    decision = slice(acc);
			    error = decision - acc;
    
			    // Update taps
			    for (i=0; i < d_ff_taps.size(); i++)
			      d_ff_taps[i] += d_lambda_ff * error * d_ff_delayline[(i+d_ff_index) & ff_mask];
    
			    for (i=0; i < d_fb_taps.size(); i++)
			      d_fb_taps[i] -= d_lambda_fb * error * d_fb_delayline[(i+d_fb_index) & fb_mask];
    
			    d_fb_index = (d_fb_index - 1) & fb_mask;   	// Decrement index
			    d_ff_index = (d_ff_index - 1) & ff_mask;   	// Decrement index

			    d_fb_delayline[d_fb_index] = decision; 	// Save decision in feedback
			}
		}
		//printf("Resetting DFE\n");
	}

	d_resetcounter++;

    acc = 0; 
    d_ff_delayline[d_ff_index] = *iptr++;

    // Compute output
    for (i=0; i < d_ff_taps.size(); i++) 
      acc += d_ff_delayline[(i+d_ff_index) & ff_mask] * d_ff_taps[i];
    
    for (i=0; i < d_fb_taps.size(); i++)
      acc -= d_fb_delayline[(i+d_fb_index) & fb_mask] * d_fb_taps[i];

    decision = slice(acc);
    error = decision - acc;
    
    // Update taps
    for (i=0; i < d_ff_taps.size(); i++)
      d_ff_taps[i] += d_lambda_ff * error * d_ff_delayline[(i+d_ff_index) & ff_mask];
    
    for (i=0; i < d_fb_taps.size(); i++)
      d_fb_taps[i] -= d_lambda_fb * error * d_fb_delayline[(i+d_fb_index) & fb_mask];
    
    d_fb_index = (d_fb_index - 1) & fb_mask;   	// Decrement index
    d_ff_index = (d_ff_index - 1) & ff_mask;   	// Decrement index

    d_fb_delayline[d_fb_index] = decision; 	// Save decision in feedback

    *optr++ = acc;   // Output decision
  }

  if (0){
    std::cout << "FF Taps\t";
    for(i=0;i<d_ff_taps.size();i++)
      std::cout << d_ff_taps[i] << "\t";
    std::cout << std::endl << "FB Taps\t";
    for(i=0;i<d_fb_taps.size();i++)
      std::cout << d_fb_taps[i] << "\t";
    std::cout << std::endl;
  }

  return noutput_items;
}
