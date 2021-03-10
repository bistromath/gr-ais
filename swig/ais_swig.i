/* -*- c++ -*- */

#define GR_AIS_API

%include "gnuradio.i"			// the common stuff

%{
#include "gnuradio/ais/freqest.h"
#include "gnuradio/ais/pdu_to_nmea.h"
%}


%include "gnuradio/ais/freqest.h"
GR_SWIG_BLOCK_MAGIC2(ais, freqest);
%include "gnuradio/ais/pdu_to_nmea.h"
GR_SWIG_BLOCK_MAGIC2(ais, pdu_to_nmea);
