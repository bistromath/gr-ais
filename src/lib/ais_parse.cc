/*
 * Copyright 2004,2006,2007 Free Software Foundation, Inc.
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <ais_parse.h>
#include <gr_io_signature.h>
#include <ctype.h>
#include <iostream>
#include <iomanip>

ais_parse_sptr ais_make_parse(gr_msg_queue_sptr queue, char designator)
{
    return ais_parse_sptr(new ais_parse(queue, designator));
}

ais_parse::ais_parse(gr_msg_queue_sptr queue, char designator) :
    gr_sync_block("parse",
    	gr_make_io_signature(1, 1, sizeof(char)),
    	gr_make_io_signature(0, 0, 0)),
    d_queue(queue),
	d_designator(designator)
{
    d_distance = 0;
	d_datacount = 0;
	d_state = AIS_IDLE;

	d_num_stoplost = 0;
	d_num_startlost = 0;
	d_num_found = 0;
//	set_output_multiple(400);
}

int ais_parse::work(int noutput_items,
    gr_vector_const_void_star &input_items,
    gr_vector_void_star &output_items)
{
	const char *in = (const char *) input_items[0];
    
    int i = 0;
//	printf("Invoked with %i requested outputs\n", noutput_items);
    while (i < noutput_items) {
		if(in[i] & 0x04) {
			//printf("Preamble correlator hit at %lu\n", samplenum);			
			//if(d_distance > (168+24+8) && d_distance <= (168+24+8+4)) printf("!, distance: %i\n", d_distance);
			d_distance = 0;
			d_state = AIS_WAITING_FOR_START; //this will reset things if a stop code is never found
		} else {
			if(in[i] & 0x02 && d_distance >= 0) { //if a start/end code was found
				if((d_state == AIS_WAITING_FOR_START) //and we have recently found a preamble
			      && (d_distance >= 5) //somewhat recently
			      && (d_distance < 30)) {
					//printf("Found start code at %lu\n", samplenum);
					d_state = AIS_DATA;
					d_datacount = 0;
					//printf("outputting start code: ");
					//for(int j = -16; j < 24; j++) printf("%i", in[i+j]);
					//printf("\n");
				} else if((d_state == AIS_DATA) //and an end code was found
				   && (d_datacount >= 192)) {
					//printf("Found end code at %lu after %i bits, totals: %i found, %i start lost, %i stop lost\n", samplenum, d_datacount, d_num_found, d_num_startlost, d_num_stoplost);
					d_state = AIS_PARSING;
					parse_data(); //data's all in the d_bits[]
					d_datacount = 0;
					d_state = AIS_IDLE;

					d_num_found++;
				}
			}
		}
		if(d_datacount > 448) {
			d_state = AIS_IDLE; //missed it
			d_datacount = 0;
			//printf("Falling through to idle, never found stop code\n");
			d_num_stoplost++;
		}
		if(d_state == AIS_DATA) {
			d_bits[d_datacount] = in[i] & 0x01;
			d_datacount++;
		}
		if(d_state == AIS_WAITING_FOR_START) 
			if(d_distance > 30) { 
				d_state = AIS_IDLE;
				//printf("Falling through to idle, never found start code\n");
				d_num_startlost++;
			}
		else d_distance++;
		i++;
		samplenum++; //a UID for debugging
    }
    return i; //be sure to let the caller know how many items you've processed
}

void ais_parse::parse_data()
{
	//okay, instead of parsing it in C, you should set this up to spit out AIVDM sentences according to the NMEA standard.
	//then use ESR's AIS decoder in gpsd to decode it
	//much more flexible

	d_payload.str("");

	//int d_message_id = 0;
	//int debug = 0;

	char asciidata[255]; //168/6 bits per ascii char
	//char sentence[255]; //how long is it really?

	reverse_bit_order(d_bits, d_datacount);

	//here, it would be a good idea if you fail the CRC to do a brute-force to swap a single bit (or even two bits, although 168*168=28K possibilities to try) to try to rescue the packet.
	//after testing, this almost NEVER rescues packets, and the reduction in CRC strength doesn't appear to be worth the possibility of occasional recovered packets.
	if(crc(d_bits, d_datacount-8)) {
//		debug = 1;
//		if(bitswap(d_bits, d_datacount-8)) {
			return; //don't make a message if bitswap fails
//		}
	}
	
//	if(debug) d_payload << "Rescued: ";
	//printf("Datacount: %i\n", d_datacount);

	//d_payload << "CRC: " << std::hex << crc(d_bits, 168) << " calc, " << unpack(d_bits, 168, 16) << " given" << std::dec << std::endl;

	for(int i = 0; i < d_datacount/6; i++) {
		asciidata[i] = unpack(d_bits, i*6, 6);
		if(asciidata[i] > 39) asciidata[i] += 8;
		asciidata[i] += 48;
	}
	

	//hey just a note, NMEA sentences are limited to 82 characters. the 448-bit long AIS messages end up longer than 82 encoded chars.
	//so technically, the below is not valid as it does not split long sentences for you. the upside is that ESR's GPSD (recommended for this use)
	//ignores this length restriction and parses them anyway. but this might bite you if you use this program with other parsers.
	//you should probably write something to split the sentences here. shouldn't be hard at all.
	//if(debug) d_payload << "BAD PACKET: ";
	d_payload << "!AIVDM,1,1,," << d_designator << ",";
	for(int i = 0; i < d_datacount/6; i++) d_payload << asciidata[i];
	d_payload << ",0"; //number of bits to fill out 6-bit boundary

	//okay. the AIS CRC and the NMEA 0183 checksum are DIFFERENT THINGS. you VALIDATE the AIS CRC, and you CALCULATE the NMEA 0183 checksum.

	char checksum = nmea_checksum(std::string(d_payload.str()));
	d_payload << "*" << std::hex << int(checksum);


	gr_message_sptr msg = gr_make_message_from_string(std::string(d_payload.str()));
	d_queue->handle(msg);
}

unsigned long ais_parse::unpack(int *buffer, int start, int length)
{
	unsigned long ret = 0;
	for(int i = start; i < (start+length); i++) {
		ret <<= 1;
		ret |= (buffer[i] & 0x01);
	}
	return ret;
}

void ais_parse::reverse_bit_order(int *data, int length)
{
	int tmp = 0;
	for(int i = 0; i < length/8; i++) {
		for(int j = 0; j < 4; j++) {
			tmp = data[i*8 + j];
			data[i*8 + j] = data[i*8 + 7-j];
			data[i*8 + 7-j] = tmp;
		}
	}
}

int ais_parse::bitswap(int *data, unsigned int len)
{
	//this swaps two bits of an incoming packet to try to "rescue" the data in the presence of a single or double bit error.
	//technically it weakens the CRC to 6 bits or less bit hey with a shitty demodulator it gets me a lot of packets back
	//on second thought most of these packets i retrieve are faulty so i'm going to stick to single bit errors
	//which, while decreasing the CRC strength, still gets the odd packet. most of my problem i believe stems from bad clock
	//recovery or transmissions stomping on each other. don't have a good handle on it. i'd think the synchronization bits
	//would be enough to get a decent clock for a while.
	for(unsigned int i = 0; i < len; i++) {
		data[i] = data[i] ? 0 : 1; //swap bits
		if(!crc(data, len)) {
			//printf("Bitswap rescued a packet with an error at %i with datalen %i\n", i, len);			
			return 0;
		} else {
			data[i] = data[i] ? 0 : 1;
		}
	}
	//printf("Unrecoverable packet of length %i\n", len);
	return 1;
}

char ais_parse::nmea_checksum(std::string buffer)
{
	unsigned int i = 0;
	char sum = 0x00;
	if(buffer[0] == '!') i++;
	for(; i < buffer.length(); i++) sum ^= buffer[i];
	return sum;
}

unsigned short ais_parse::crc(int *buffer, unsigned int len) // Calculates CRC-checksum from unpacked data
{
	static const uint16_t crc_itu16_table[] =
	{
    0x0000, 0x1189, 0x2312, 0x329B, 0x4624, 0x57AD, 0x6536, 0x74BF,
    0x8C48, 0x9DC1, 0xAF5A, 0xBED3, 0xCA6C, 0xDBE5, 0xE97E, 0xF8F7,
    0x1081, 0x0108, 0x3393, 0x221A, 0x56A5, 0x472C, 0x75B7, 0x643E,
    0x9CC9, 0x8D40, 0xBFDB, 0xAE52, 0xDAED, 0xCB64, 0xF9FF, 0xE876,
    0x2102, 0x308B, 0x0210, 0x1399, 0x6726, 0x76AF, 0x4434, 0x55BD,
    0xAD4A, 0xBCC3, 0x8E58, 0x9FD1, 0xEB6E, 0xFAE7, 0xC87C, 0xD9F5,
    0x3183, 0x200A, 0x1291, 0x0318, 0x77A7, 0x662E, 0x54B5, 0x453C,
    0xBDCB, 0xAC42, 0x9ED9, 0x8F50, 0xFBEF, 0xEA66, 0xD8FD, 0xC974,
    0x4204, 0x538D, 0x6116, 0x709F, 0x0420, 0x15A9, 0x2732, 0x36BB,
    0xCE4C, 0xDFC5, 0xED5E, 0xFCD7, 0x8868, 0x99E1, 0xAB7A, 0xBAF3,
    0x5285, 0x430C, 0x7197, 0x601E, 0x14A1, 0x0528, 0x37B3, 0x263A,
    0xDECD, 0xCF44, 0xFDDF, 0xEC56, 0x98E9, 0x8960, 0xBBFB, 0xAA72,
    0x6306, 0x728F, 0x4014, 0x519D, 0x2522, 0x34AB, 0x0630, 0x17B9,
    0xEF4E, 0xFEC7, 0xCC5C, 0xDDD5, 0xA96A, 0xB8E3, 0x8A78, 0x9BF1,
    0x7387, 0x620E, 0x5095, 0x411C, 0x35A3, 0x242A, 0x16B1, 0x0738,
    0xFFCF, 0xEE46, 0xDCDD, 0xCD54, 0xB9EB, 0xA862, 0x9AF9, 0x8B70,
    0x8408, 0x9581, 0xA71A, 0xB693, 0xC22C, 0xD3A5, 0xE13E, 0xF0B7,
    0x0840, 0x19C9, 0x2B52, 0x3ADB, 0x4E64, 0x5FED, 0x6D76, 0x7CFF,
    0x9489, 0x8500, 0xB79B, 0xA612, 0xD2AD, 0xC324, 0xF1BF, 0xE036,
    0x18C1, 0x0948, 0x3BD3, 0x2A5A, 0x5EE5, 0x4F6C, 0x7DF7, 0x6C7E,
    0xA50A, 0xB483, 0x8618, 0x9791, 0xE32E, 0xF2A7, 0xC03C, 0xD1B5,
    0x2942, 0x38CB, 0x0A50, 0x1BD9, 0x6F66, 0x7EEF, 0x4C74, 0x5DFD,
    0xB58B, 0xA402, 0x9699, 0x8710, 0xF3AF, 0xE226, 0xD0BD, 0xC134,
    0x39C3, 0x284A, 0x1AD1, 0x0B58, 0x7FE7, 0x6E6E, 0x5CF5, 0x4D7C,
    0xC60C, 0xD785, 0xE51E, 0xF497, 0x8028, 0x91A1, 0xA33A, 0xB2B3,
    0x4A44, 0x5BCD, 0x6956, 0x78DF, 0x0C60, 0x1DE9, 0x2F72, 0x3EFB,
    0xD68D, 0xC704, 0xF59F, 0xE416, 0x90A9, 0x8120, 0xB3BB, 0xA232,
    0x5AC5, 0x4B4C, 0x79D7, 0x685E, 0x1CE1, 0x0D68, 0x3FF3, 0x2E7A,
    0xE70E, 0xF687, 0xC41C, 0xD595, 0xA12A, 0xB0A3, 0x8238, 0x93B1,
    0x6B46, 0x7ACF, 0x4854, 0x59DD, 0x2D62, 0x3CEB, 0x0E70, 0x1FF9,
    0xF78F, 0xE606, 0xD49D, 0xC514, 0xB1AB, 0xA022, 0x92B9, 0x8330,
    0x7BC7, 0x6A4E, 0x58D5, 0x495C, 0x3DE3, 0x2C6A, 0x1EF1, 0x0F78
	};	

	unsigned short crc=0xffff;
	int i = 0;

	int datalen = len/8;

	char data[256];
	for(int j=0;j<datalen;j++) //this unpacks the data in preparation for calculating CRC
	{
		data[j] = unpack(buffer, j*8, 8);
	}

    for (i = 0;  i < datalen;  i++)
        crc = (crc >> 8) ^ crc_itu16_table[(crc ^ data[i]) & 0xFF];

    return (crc & 0xFFFF) != 0xF0B8;
//(crc & 0xFFFF) == 0xF0B8;
}
