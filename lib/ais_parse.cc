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
#include <boost/foreach.hpp>
#include <gr_tags.h>
#include <gr_ais_api.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#define VERBOSE  0
#define VERBOSE2 1

GR_AIS_API ais_parse_sptr ais_make_parse(gr_msg_queue_sptr queue, char designator)
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
    d_num_stoplost = 0;
    d_num_startlost = 0;
    d_num_found = 0;
    set_output_multiple(1000);
}

int ais_parse::work(int noutput_items,
    gr_vector_const_void_star &input_items,
    gr_vector_void_star &output_items)
{
    const char *in = (const char *) input_items[0];

    int size = noutput_items - 500; //we need to be able to look at least this far forward
    if(size <= 0) return 0;

    //look ma, no state machine
    //instead of iterating through in[] looking for things, we'll just pull up all the start/stop tags and use those to look for packets
    std::vector<gr_tag_t> preamble_tags, start_tags, end_tags;
    uint64_t abs_sample_cnt = nitems_read(0);
    get_tags_in_range(preamble_tags, 0, abs_sample_cnt, abs_sample_cnt + size, pmt::pmt_string_to_symbol("ais_preamble"));
    if(preamble_tags.size() == 0) return size; //sad trombone
    
    //look for start & end tags within a reasonable range
    uint64_t preamble_mark = preamble_tags[0].offset;
    if(VERBOSE) std::cout << "Found a preamble at " << preamble_mark << std::endl;
    
    //now look for a start tag within reasonable range of the preamble
    get_tags_in_range(start_tags, 0, preamble_mark, preamble_mark + 30, pmt::pmt_string_to_symbol("ais_frame"));
    if(start_tags.size() == 0) return preamble_mark + 30 - abs_sample_cnt; //nothing here, move on (should update d_num_startlost)
    uint64_t start_mark = start_tags[0].offset;
    if(VERBOSE) std::cout << "Found a start tag at " << start_mark << std::endl;
    
    //now look for an end tag within reasonable range of the preamble
    get_tags_in_range(end_tags, 0, start_mark + 184, start_mark + 450, pmt::pmt_string_to_symbol("ais_frame"));
    if(end_tags.size() == 0) return preamble_mark + 450 - abs_sample_cnt; //should update d_num_stoplost
    uint64_t end_mark = end_tags[0].offset;
    if(VERBOSE) std::cout << "Found an end tag at " << end_mark << std::endl;

    //now we've got a valid, framed packet
    uint64_t datalen = end_mark - start_mark - 8; //includes CRC, discounts end of frame marker
    if(VERBOSE) std::cout << "Found packet with length " << datalen << std::endl;
    char *pkt = new char[datalen];

    memcpy(pkt, &in[start_mark-abs_sample_cnt], datalen);
    parse_data(pkt, datalen);
    delete(pkt);
    return end_mark - abs_sample_cnt;
}

void ais_parse::parse_data(char *data, int len)
{
    d_payload.str("");

    char asciidata[255]; //168/6 bits per ascii char

    reverse_bit_order(data, len); //the AIS standard has bits come in backwards for some inexplicable reason

    if(crc(data, len)) {
        if(VERBOSE)
           std::cout << "Failed CRC!" << std::endl;

        return; //don't make a message if crc fails
    }

    len -= 16; //strip off CRC

    for(int i = 0; i < len/6; i++) {
        asciidata[i] = unpack(data, i*6, 6);

        //printf("%02x ", asciidata[i]);

        if(asciidata[i] > 39)
            asciidata[i] += 8;
        asciidata[i] += 48;
    }

    //printf("\n");

    //hey just a note, NMEA sentences are limited to 82 characters. the 448-bit long AIS messages end up longer than 82 encoded chars.
    //so technically, the below is not valid as it does not split long sentences for you. the upside is that ESR's GPSD (recommended for this use)
    //ignores this length restriction and parses them anyway. but this might bite you if you use this program with other parsers.
    //you should probably write something to split the sentences here. shouldn't be hard at all.
    //if(debug) d_payload << "BAD PACKET: ";
    d_payload << "!AIVDM,1,1,," << d_designator << ",";
    for(int i = 0; i < len/6; i++) d_payload << asciidata[i];
    d_payload << ",0"; //number of bits to fill out 6-bit boundary

    char checksum = nmea_checksum(std::string(d_payload.str()));
    d_payload << "*" << std::setw(2) << std::setfill('0') << std::hex << std::uppercase << int(checksum);

    //ptooie
    gr_message_sptr msg = gr_make_message_from_string(std::string(d_payload.str()));
    d_queue->handle(msg);

    decode_ais(asciidata, len/6);
}

void ais_parse::decode_ais(char *ascii, int len)
{
    // http://gpsd.berlios.de/AIVDM.html#_types_1_2_and_3_position_report_class_a
    // http://rl.se/aivdm

    // check report type
    unsigned long value;
    int report_type;

    value = ascii_to_ais(*ascii);
    if(value > 27) {
        if(VERBOSE2)
            printf("Unknown AIS report type: %d", value);

        return;
    }

    d_payload.str("");

    unsigned char *data = (unsigned char *) malloc(len * sizeof(unsigned char));
    char *str = (char *) malloc(1024 * sizeof(char));
    bool error;

    double d_value;
    int i;

    for(i=0; i<len; i++) {
        data[i] = ascii_to_ais(ascii[i]);
        if(VERBOSE2) {
            printf("%02x ", data[i]);
            if(i == (len-1))
                printf("\n");
        }
    }

    // message type bit 0-5 len 6 bit
    report_type = *data;

    switch(report_type) {
    case  1: d_payload << "Position Report Class A\n"; break;
    case  2: d_payload << "Position Report Class A (Assigned schedule)\n"; break;
    case  3: d_payload << "Position Report Class A (Response to interrogation)\n"; break;
    case  4: d_payload << "Base Station Report\n"; break;
    case  5: d_payload << "Static and Voyage Related Data\n"; break;
    case  6: d_payload << "Binary Addressed Message\n"; break;
    case  7: d_payload << "Binary Acknowledge\n"; break;
    case  8: d_payload << "Binary Broadcast Message\n"; break;
    case  9: d_payload << "Standard SAR Aircraft Position Report\n"; break;
    case 10: d_payload << "UTC and Date Inquiry\n"; break;
    case 11: d_payload << "UTC and Date Response\n"; break;
    case 12: d_payload << "Addressed Safety Related Message\n"; break;
    case 13: d_payload << "Safety Related Acknowledgement\n"; break;
    case 14: d_payload << "Safety Related Broadcast Message\n"; break;
    case 15: d_payload << "Interrogation\n"; break;
    case 16: d_payload << "Assignment Mode Command\n"; break;
    case 17: d_payload << "DGNSS Binary Broadcast Message\n"; break;
    case 18: d_payload << "Standard Class B CS Position Report\n"; break;
    case 19: d_payload << "Extended Class B Equipment Position Report\n"; break;
    case 20: d_payload << "Data Link Management\n"; break;
    case 21: d_payload << "Aid-to-Navigation Report\n"; break;
    case 22: d_payload << "Channel Management\n"; break;
    case 23: d_payload << "Group Assignment Command\n"; break;
    case 24: d_payload << "Static Data Report\n"; break;
    case 25: d_payload << "Single Slot Binary Message\n"; break;
    case 26: d_payload << "Multiple Slot Binary Message With Communications State\n"; break;
    case 27: d_payload << "Position Report For Long-Range Applications\n"; break;

    default:
        d_payload << "Unknown report type " << report_type << "\n";
    }

    // skip repeat indicator bit 6-7 len 2

    // Mobile Marine Service Identifier bit 8-37 len 30
    value = (data[1] & 0x0f) << 26 | data[2] << 20 | data[3] << 14 | data[4] << 8 | data[5] << 2 | ((data[6] >> 4) & 0x03);
    sprintf(str, "Mobile Marine Service Identifier: %d\n", value);
    d_payload << str;

    switch(report_type) {
    case 4: decode_base_station(data+6, len, str); break;

    //default: { // nop }
    }

#if 0

    // Navigation Status bit 38-41 len 4
    value = data[6] & 0x0f;
    error = false;

    switch(value) {
    case  0: strcpy(str, "Navigation Status: Under way using engine\n"); break;
    case  1: strcpy(str, "Navigation Status: At anchor\n"); break;
    case  2: strcpy(str, "Navigation Status: Not under command\n"); break;
    case  3: strcpy(str, "Navigation Status: Restricted manoeuverability\n"); break;
    case  4: strcpy(str, "Navigation Status: Constrained by her draught\n"); break;
    case  5: strcpy(str, "Navigation Status: Moored\n"); break;
    case  6: strcpy(str, "Navigation Status: Aground\n"); break;
    case  7: strcpy(str, "Navigation Status: Engaged in Fishing\n"); break;
    case  8: strcpy(str, "Navigation Status: Under way sailing\n"); break;

    // skip reserved 9-4 and undefined 15
    default:
        error = true;
    }

    if(!error)
        d_payload << str;

    // Rate of Turn (ROT) 42-49 8 bit
    value = data[7] << 2 | ((data[8] >> 4) & 0x03);
    i = (signed char) value;
    //printf("Rate of Turn: %d (%d)\n", (signed char) value, value);

    error = false;
    if(i == 0)
        strcpy(str, "Rate of Turn: Not turning\n");
    else if(i == 127)
        strcpy(str, "Rate of Turn: Right at more than 5 deg per 30 s\n");
    else if(i == -127)
        strcpy(str, "Rate of Turn: Left at more than 5 deg per 30 s\n");
    else if(abs(i) == 128)
        error = true;
    else {
        d_value = pow(((double) i) / 4.733, 2);
        sprintf(str, "Rate of Turn: %s at %.3f deg/s\n", i > 0 ? "Right":"Left", d_value);
    }

    if(!error)
        d_payload << str;

    // Speed Over Ground (SOG) 50-59 10 bit
    value = (data[8] & 0x0f) << 6 | data[9];
    sprintf(str, "Speed Over Ground: %.1f knots (%d)\n", (double) value / 10.0, value);
    d_payload << str;
#endif

    d_payload << std::endl;

    free(data);
    free(str);

    gr_message_sptr msg = gr_make_message_from_string(std::string(d_payload.str()));
    d_queue->handle(msg);

}

void ais_parse::decode_base_station(unsigned char *ais, int len, char *str)
{
    if((len*6) != 168) {
        if(VERBOSE2)
            printf("Erroneous report size %d bit, it should be 168 bit", len*6);

        return;
    }

    unsigned int v1, v2, v3, v4;
    double d;

    // utc year 38-51 14 bit
    v1 = (ais[0] & 0x0f) << 10 | ais[1] << 4 | (ais[2] & 0x3c) >> 2;
    // utc month 52-55 4 bit
    v2 = ((ais[2] & 0x03) << 2 | ais[3] & 0x30);
    // utc day 56-60 5 bit
    v3 = ((ais[3] & 0x0f) << 1 | (ais[4] & 0x10) >> 5);
    // utc hour 61-65 5 bit
    v4 = ais[4] & 0x1f;
    // utc minute 66-71 6 bit
    // utc second 72-77 6 bit
    sprintf(str, "%d-%02d-%02d %02d:%02d:%02d UTC\n", v1, v2, v3, v4, ais[5], ais[6]);
    d_payload << str;

    // skip fix 1 bit
    // lon 79-106 28 bit
    //       5                     6              6              6                 5
    v1 = (ais[7] & 0x1f) << 23 | ais[8] << 17 | ais[9] << 11 | ais[10] << 5 | (ais[11] & 0x3e) >> 1; // 1 bit
    d = ((double) v1) / 600000.0;
    sprintf(str, "Longitude: %.6f %s\n", fabs(d), d < 0 ? "West":"East");
    d_payload << str;

    // lat 107-133 27 bit
    //       1                     6               6               6               6                  2
    v1 = (ais[11] & 0x01) << 26 | ais[12] << 20 | ais[13] << 14 | ais[14] << 8 | ais[15] << 2 | (ais[16] & 0x30) >> 4; // 4 bit
    d = ((double) v1) / 600000.0;
    sprintf(str, "Latitude: %.6f %s\n", fabs(d), d < 0 ? "South":"North");
    d_payload << str;

}

unsigned long ais_parse::unpack(char *buffer, int start, int length)
{
    unsigned long ret = 0;
    for(int i = start; i < (start+length); i++) {
	ret <<= 1;
	ret |= (buffer[i] & 0x01);
    }
    return ret;
}

void ais_parse::reverse_bit_order(char *data, int length)
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

char ais_parse::nmea_checksum(std::string buffer)
{
    unsigned int i = 0;
    char sum = 0x00;
    if(buffer[0] == '!') i++;
    for(; i < buffer.length(); i++) sum ^= buffer[i];
    return sum;
}

unsigned short ais_parse::crc(char *buffer, unsigned int len) // Calculates CRC-checksum from unpacked data
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
}
