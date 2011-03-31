/*
 * Copyright 2006 Free Software Foundation, Inc.
 * 
 * This file is part of GNU Radio
 * 
 * GNU Radio is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3, or (at your option)
 * any later version.
 * 
 * GNU Radio is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with GNU Radio; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street,
 * Boston, MA 02110-1301, USA.
 */

#ifndef INCLUDED_AIS_PARSE_H
#define INCLUDED_AIS_PARSE_H

#include <gr_sync_block.h>
#include <gr_msg_queue.h>
//#include <pageri_flex_modes.h>
#include <sstream>

class ais_parse;
typedef boost::shared_ptr<ais_parse> ais_parse_sptr;

ais_parse_sptr ais_make_parse(gr_msg_queue_sptr queue, char designator);

/*!
 * \brief flex parse description
 * \ingroup block
 */

#define FIELD_DELIM ((unsigned char)128)

#define AIS_IDLE 0x00
#define AIS_WAITING_FOR_START 0x01
#define AIS_DATA 0x02
#define AIS_PARSING 0x04

class ais_parse : public gr_sync_block
{
private:
    // Constructors
    friend ais_parse_sptr ais_make_parse(gr_msg_queue_sptr queue, char designator);
    ais_parse(gr_msg_queue_sptr queue, char designator);

    std::ostringstream d_payload; //message output
	int d_bits[512]; //bits of data
//	char d_unpacked_data[512]; //decoded bits
    gr_msg_queue_sptr d_queue;		  // Destination for decoded messages

    int d_distance;
	bool d_in_packet;
	float d_freq;
	unsigned long samplenum;
	int d_state;
	int d_datacount;
	char d_designator;

	int d_num_stoplost;
	int d_num_startlost;
	int d_num_found;

	void parse_data();
	void reverse_bit_order(int *data, int length);
	unsigned short crc(int *buffer, unsigned int len);
	unsigned long unpack(int *buffer, int start, int length);
	char nmea_checksum(std::string buffer);
	int bitswap(int *data, unsigned int len);
    
public:
    int work(int noutput_items,
        gr_vector_const_void_star &input_items, 
        gr_vector_void_star &output_items);
};

#endif /* INCLUDED_AIS_PARSE_H */
