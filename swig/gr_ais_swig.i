/* -*- c++ -*- */

#define GR_AIS_API

%include "gnuradio.i"			// the common stuff

%{
#include "ais_invert.h"
#include "ais_unstuff.h"
#include "ais_parse.h"
#include "ais_extended_lms_dfe_ff.h"
#include "ais_freqest.h"
%}

//load generated python docstrings
%include "gr_ais_swig_doc.i"

GR_SWIG_BLOCK_MAGIC(ais,invert);

ais_invert_sptr ais_make_invert();

class ais_invert : public gr_sync_block
{
private:
	ais_invert();

public:
};

GR_SWIG_BLOCK_MAGIC(ais,unstuff);

ais_unstuff_sptr ais_make_unstuff();

class ais_unstuff : public gr_block
{
private:
	ais_unstuff();

public:
};

GR_SWIG_BLOCK_MAGIC(ais,parse);

ais_parse_sptr ais_make_parse(gr_msg_queue_sptr queue, char designator, int verbose, double lon, double lat);

class ais_parse : public gr_sync_block
{
private:
    ais_parse(gr_msg_queue_sptr queue, char designator, int verbose, double lon, double lat);

public:
};

GR_SWIG_BLOCK_MAGIC(ais,extended_lms_dfe_ff);

ais_extended_lms_dfe_ff_sptr ais_make_extended_lms_dfe_ff(float lambda_ff, float lambda_fb , 
			      unsigned int num_fftaps, unsigned int num_fbtaps);

class ais_extended_lms_dfe_ff : public gr_sync_block
{
private:
	ais_extended_lms_dfe_ff(float lambda_ff, float lambda_fb , 
			      unsigned int num_fftaps, unsigned int num_fbtaps);

public:
};

GR_SWIG_BLOCK_MAGIC(ais,freqest);

ais_freqest_sptr ais_make_freqest(int sample_rate, int data_rate, int fftlen);

class ais_freqest : public gr_sync_block
{
private:
	ais_freqest(int sample_rate, int data_rate, int fftlen);

public:
};
