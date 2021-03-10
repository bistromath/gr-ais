/* -*- c++ -*- */

#define AIS_API

%include "gnuradio.i"			// the common stuff

%{
#include "ais/freqest.h"
#include "ais/pdu_to_nmea.h"
%}


%include "ais/freqest.h"
GR_SWIG_BLOCK_MAGIC2(ais, freqest);
%include "ais/pdu_to_nmea.h"
GR_SWIG_BLOCK_MAGIC2(ais, pdu_to_nmea);
