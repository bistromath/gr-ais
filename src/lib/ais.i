/* -*- c++ -*- */

%include "gnuradio.i"			// the common stuff

%{
#include "ais_invert.h"
#include "ais_unstuff.h"
#include "ais_parse.h"
#include "ais_shift.h"
#include "ais_extended_lms_dfe_ff.h"
%}

// ----------------------------------------------------------------

/*
 * First arg is the package prefix.
 * Second arg is the name of the class minus the prefix.
 *
 * This does some behind-the-scenes magic so we can
 * access howto_square_ff from python as howto.square_ff
 */
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

ais_parse_sptr ais_make_parse(gr_msg_queue_sptr queue, char designator);

class ais_parse : public gr_sync_block
{
private:
	ais_parse(gr_msg_queue_sptr queue, char designator);

public:
};

GR_SWIG_BLOCK_MAGIC(ais,shift);

ais_shift_sptr ais_make_shift();

class ais_shift : public gr_sync_block
{
private:
	ais_shift();

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
