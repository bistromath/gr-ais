/*
 * Copyright 2004,2006,2007,2013 Free Software Foundation, Inc.
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

/*
   Useful AIS packet documents

   http://www.navcen.uscg.gov/?pageName=AISmain
   http://gpsd.berlios.de/AIVDM.html#_types_1_2_and_3_position_report_class_a
   http://rl.se/aivdm
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

const char   *DEG_SIGN = "\xc2\xb0";
const double DTR = M_PI / 180.0;
const double RTD = 180.0 / M_PI;

static const char ais_ascii_table[64] =
{
    '@', 'A', 'B', 'C', 'D', 'E', 'F', 'G',
    'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O',
    'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W',
    'X', 'Y', 'Z', '[', '\\', ']', '^', '_',
    ' ', '!', '"', '#', '$', '%', '&', '\'',
    '(', ')', '*', '+', ',', '-', '.', '/',
    '0', '1', '2', '3', '4', '5', '6', '7',
    '8', '9', ':', ';', '<', '=', '>', '?'
};


GR_AIS_API ais_parse_sptr ais_make_parse(gr_msg_queue_sptr queue, char designator, int verbose, double lon, double lat)
{
    return ais_parse_sptr(new ais_parse(queue, designator, verbose, lon, lat));
}

ais_parse::ais_parse(gr_msg_queue_sptr queue, char designator, int verbose, double lon, double lat) :
    gr_sync_block("parse",
    	gr_make_io_signature(1, 1, sizeof(char)),
    	gr_make_io_signature(0, 0, 0)),
    d_queue(queue),
    d_designator(designator)
{
    d_num_stoplost = 0;
    d_num_startlost = 0;
    d_num_found = 0;

    // defaults to Vaasa, Finland, poes-weather
    d_qth_lon = lon < -180 || lon > 180 ? 21.5593:lon;
    d_qth_lat = lat < -90 || lat > 90 ? 63.1587:lat;

    unsigned level = ((unsigned) verbose) & V_MAX_LEVEL;
    d_verbose = 0;
    for(unsigned i=0; i<level; i++)
        d_verbose |= 1 << i;

    if(d_verbose & V_DEBUG_5) {
        std::cout << "verbose level " << verbose << " debug level " << d_verbose << std::endl;
        std::cout << "your latitude: " << d_qth_lat << std::endl;
        std::cout << "your longitude: " << d_qth_lon << std::endl;
    }

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
    if(preamble_tags.size() == 0)
        return size; //sad trombone
    
    //look for start & end tags within a reasonable range
    uint64_t preamble_mark = preamble_tags[0].offset;
    if(d_verbose & V_DEBUG_6)
        std::cout << d_designator << " Found a preamble at " << preamble_mark << std::endl;
    
    //now look for a start tag within reasonable range of the preamble
    get_tags_in_range(start_tags, 0, preamble_mark, preamble_mark + 30, pmt::pmt_string_to_symbol("ais_frame"));
    if(start_tags.size() == 0)
        return (preamble_mark + 30 - abs_sample_cnt); //nothing here, move on (should update d_num_startlost)

    uint64_t start_mark = start_tags[0].offset;
    if(d_verbose & V_DEBUG_5)
        std::cout << d_designator << " Found a start tag at " << start_mark << std::endl;
    
    //now look for an end tag within reasonable range of the preamble
    get_tags_in_range(end_tags, 0, start_mark + 184, start_mark + 450, pmt::pmt_string_to_symbol("ais_frame"));
    if(end_tags.size() == 0)
        return (preamble_mark + 450 - abs_sample_cnt); //should update d_num_stoplost

    uint64_t end_mark = end_tags[0].offset;
    if(d_verbose & V_DEBUG_5)
        std::cout << d_designator << " Found an end tag at " << end_mark << std::endl;

    //now we've got a valid, framed packet
    uint64_t datalen = end_mark - start_mark - 8; //includes CRC, discounts end of frame marker
    if(d_verbose & V_DEBUG_4)
        std::cout << d_designator << " Found packet with length (-crc 16 bit) " << (datalen - 16) << "\n" << std::endl;

    char *pkt = new char[datalen];

    memcpy(pkt, &in[start_mark-abs_sample_cnt], datalen);
    parse_data(pkt, datalen);

    delete(pkt);

    return (end_mark - abs_sample_cnt);
}

void ais_parse::parse_data(char *data, int len)
{
    d_payload.str("");

    char asciidata[255]; // 6 bits per ascii char (168/6 = 28 chars (bytes))

    reverse_bit_order(data, len); //the AIS standard has bits come in backwards for some inexplicable reason

    bool crc_chk = true;
    if(crc(data, len)) {
        crc_chk = false;

        if(!(d_verbose & V_DEBUG_2))
            return; //don't make a message if crc fails
    }

    len -= 16; //strip off CRC
    int len6 = len / 6;

    for(int i = 0; i < len6; i++)
        asciidata[i] = data_to_ascii(unpack(data, i*6, 6));

    // hey just a note, NMEA sentences are limited to 82 characters. the 448-bit long AIS messages end up longer than 82 encoded chars.
    // so technically, the below is not valid as it does not split long sentences for you. the upside is that ESR's GPSD (recommended for this use)
    // ignores this length restriction and parses them anyway. but this might bite you if you use this program with other parsers.
    // you should probably write something to split the sentences here. shouldn't be hard at all.

    d_payload << "!AIVDM,1,1,," << d_designator << ",";
    for(int i = 0; i < len6; i++)
        d_payload << asciidata[i];

    d_payload << ",0"; //number of bits to fill out 6-bit boundary

    char checksum = nmea_checksum(std::string(d_payload.str()));
    d_payload << "*" << std::setw(2) << std::setfill('0') << std::hex << std::uppercase << int(checksum);

    if(d_verbose & V_DEBUG_2)
        d_payload << " <- " << std::string(crc_chk ? "CRC OK!":"CRC Failed!");

    //ptooie
    gr_message_sptr msg = gr_make_message_from_string(std::string(d_payload.str()));
    d_queue->handle(msg);


    // try to decode broken packet if verbose level > 2
    if(!crc_chk && !(d_verbose & V_DEBUG_3))
        return;

    if(d_verbose & V_DECODE)
        decode_ais(asciidata, len6, crc_chk);
}

/**

    AIS Packet Decoders

**/

void ais_parse::decode_ais(char *ascii, int len, bool crc_ok)
{
    unsigned char *data;
    unsigned long value;
    int report_type, i;

    d_payload.str("");

    report_type = ascii_to_ais(*ascii);
    if(report_type == 0 || report_type > 27) {
        if(!(d_verbose & V_DEBUG_3))
            return;
    }

    // allocate enough so it wont segfault when debugging
    i = (d_verbose & V_DEBUG_2) ? 256:len;
    data = (unsigned char *) malloc(i * sizeof(unsigned char));

    char *str = (char *) malloc(512 * sizeof(char));

    for(i=0; i<len; i++) {
        data[i] = ascii_to_ais(ascii[i]);

        if(d_verbose & V_DEBUG_4) {
            printf("%02x ", data[i]);
            if(i == (len-1))
                printf("<- %s\n", crc_ok ? "CRC OK!":"CRC Failed!");
        }
    }

    sprintf(str, "AIS VHF maritime channel %dB (%.3f MHz)\n",
            d_designator == 'A' ? 87:88,
            d_designator == 'A' ? 161.975:162.025);
    d_payload << str;

    value = ais_value(data, 8, 30);
    sprintf(str, "Mobile Marine Service Identifier: %d\n", value);
    d_payload << str;

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
    case 18: d_payload << "Standard Class B Carrier Sense Position Report\n"; break;
    case 19: d_payload << "Extended Class B Carrier Sense Equipment Position Report\n"; break;
    case 20: d_payload << "Data Link Management\n"; break;
    case 21: d_payload << "Aid-to-Navigation Report\n"; break;
    case 22: d_payload << "Channel Management\n"; break;
    case 23: d_payload << "Group Assignment Command\n"; break;
    case 24: d_payload << "Static Data Report\n"; break;
    case 25: d_payload << "Single Slot Binary Message\n"; break;
    case 26: d_payload << "Multiple Slot Binary Message With Communications State\n"; break;
    case 27: d_payload << "Position Report For Long-Range Applications\n"; break;

    default:
        sprintf(str, "Unknown AIS report type %d\n", report_type);
        d_payload << str;

        break;
    }

    switch(report_type) {
    case  1:
    case  2:
    case  3: decode_position_123A(data, len, str); break;
    case  4: decode_base_station(data, len, str); break;
    case  5: decode_static_and_voyage_data(data, len, str); break;

#if 0
        // todo
    case  6: decode_bin_addr_msg(data, len, str); break;
#endif

    case  9: decode_sar_aircraft_position(data, len, str); break;
    case 10:
    case 11: decode_utc_inquiry(data, len, str); break;
    case 12:
    case 13: decode_addr_ack_safety_msg(data, len, str); break;
    case 14: decode_safety_broadcast_msg(data, len, str); break;
    case 15: decode_interrogation(data, len, str); break;
    case 16: decode_assignment_mode_command(data, len, str); break;
    case 17: decode_dgnss_broadcast_bin_msg(data, len, str); break;
    case 18: decode_class_b_position_report(data, len, str, false); break;
    case 19: decode_class_b_position_report(data, len, str, true); break;
    case 21: decode_aid_to_navigation(data, len, str); break;
    case 24: decode_static_data_msg(data, len, str); break;
    case 27: decode_long_range_msg(data, len, str); break;
    }

    free(data);
    free(str);

    gr_message_sptr msg = gr_make_message_from_string(std::string(d_payload.str()));
    d_queue->handle(msg);
}

void ais_parse::decode_sar_aircraft_position(unsigned char *ais, int len, char *str)
{
    if((len*6) != 168) {
        if(d_verbose & V_DEBUG_2)
            printf("Erroneous report size %d bit, it should be 168 bit\n", len*6);

        if(!(d_verbose & V_DEBUG_3))
            return;
    }

    unsigned int v1;

    v1 = ais_value(ais, 38, 12);
    if(v1 != 4095) {
        sprintf(str, "Altitude: %d m %s", v1, v1 == 4095 ? "or higher\n":"\n");
        d_payload << str;
    }

    print_speed_over_ground(ais, 50, str, true);
    print_position(ais, 61, str, "aircraft");
    print_course_over_ground(ais, 116, str);
}

void ais_parse::decode_static_and_voyage_data(unsigned char *ais, int len, char *str)
{
    // 424/6 = 70.6667
    if((len*6) != 420) {
        if(d_verbose & V_DEBUG_2)
            printf("Erroneous report size %d bit, it should be 424 bit\n", len*6);

        if(!(d_verbose & V_DEBUG_3))
            return;
    }

    unsigned int v1;

    sprintf(str, "AIS version: %d\n", ais_value(ais, 38, 2));
    d_payload << str;

    sprintf(str, "IMO Number: %d\n", ais_value(ais, 40, 30));
    d_payload << str;

    d_payload << "Call Sign: " << get_ais_text(ais, 70, 7, str) << "\n";

    print_ship_properties(ais, 112, str);

    sprintf(str, "Draught: %.1f m\n", ((double) ais_value(ais, 294, 8)) / 10.0);
    d_payload << str;

    print_position_fix_type(ais, 270, str);

    d_payload << "Destination: " << get_ais_text(ais, 302, 20, str) << "\n";

    v1 = ais_value(ais, 274, 4);
    if(v1 != 0) {
        sprintf(str, "Estimated Time of Arrival %02d-%02d %02d:%02d UTC\n",
                v1, ais_value(ais, 278, 5),
                ais_value(ais, 283, 5), ais_value(ais, 288, 6));
        d_payload << str;
    }
}

void ais_parse::decode_position_123A(unsigned char *ais, int len, char *str)
{
    if((len*6) != 168) {
        if(d_verbose & V_DEBUG_2)
            printf("Erroneous report size %d bit, it should be 168 bit\n", len*6);

        if(!(d_verbose & V_DEBUG_3))
            return;
    }

    unsigned long value;
    double d;
    bool error;
    int i;

    print_navigation_status(ais, 38, str);

    value = ais_value(ais, 42, 8);
    i = (signed char) value;

    error = false;
    if(i < -127 || i > 127 || i == 0)
        error = true;
    if(i == 127)
        sprintf(str, "Rate of Turn: Right at more than 5%s per 30 second\n", DEG_SIGN);
    else if(i == -127)
        sprintf(str, "Rate of Turn: Left at more than 5%s per 30 second\n", DEG_SIGN);
    else {
        d = pow(((double) i) / 4.733, 2);
        sprintf(str, "Rate of Turn: %s at %.3f%s/min\n", i > 0 ? "Right":"Left", d, DEG_SIGN);
    }

    if(!error)
        d_payload << str;

    print_speed_over_ground(ais, 50, str, true);
    print_position(ais, 61, str, "vessel");
    print_course_over_ground(ais, 116, str);

    value = ais_value(ais, 128, 9);
    if(value >= 0 && value < 360) {
        sprintf(str, "True Heading: %d%s\n", value, DEG_SIGN);
        d_payload << str;
    }

    value = ais_value(ais, 137, 6);
    sprintf(str, "Time Stamp: %d seconds\n", value, DEG_SIGN);
    d_payload << str;

    value = ais_value(ais, 143, 2);
    if(value) {
        if(value == 1)
            sprintf(str, "Maneuver Indicator: No special maneuver\n");
        else
            sprintf(str, "Maneuver Indicator: Special maneuver (such as regional passing arrangement)\n");

        d_payload << str;
    }

    print_raim(ais, 148, str);
}

void ais_parse::decode_class_b_position_report(unsigned char *ais, int len, char *str, bool extended)
{
    int i = extended ? 312:168;

    if((len*6) != i) {
        if(d_verbose & V_DEBUG_2)
            printf("Erroneous report size %d bit, it should be %d bit\n", len*6, i);

        if(!(d_verbose & V_DEBUG_3))
            return;
    }

    unsigned long value;

    print_speed_over_ground(ais, 46, str, true);
    print_position(ais, 57, str, "vessel");
    print_course_over_ground(ais, 112, str);

    value = ais_value(ais, 124, 9);
    if(value >= 0 && value < 360) {
        sprintf(str, "True Heading: %d%s\n", value, DEG_SIGN);
        d_payload << str;
    }

    value = ais_value(ais, 133, 6);
    sprintf(str, "Time Stamp: %d UTC seconds\n", value);
    d_payload << str;

    if(extended) {
        print_raim(ais, 305, str);

        value = ais_value(ais, 307, 1);
        sprintf(str, "Station Mode: %s\n",
                value == 0 ? "Station operating in autonomous and continuous mode":"Station operating in assigned mode");
        d_payload << str;
    }
    else {
        value = ais_value(ais, 141, 1);
        sprintf(str, "Carrier Sense Unit: %s\n",
                value == 0 ? "Class B SOTDMA":"Class B Carrier Sense");
        d_payload << str;

        value = ais_value(ais, 142, 1);
        sprintf(str, "Display: %s\n",
                value == 0 ? "No display available. Not capable of displaying Message 12 and 14":"Equipped with integrated display displaying Message 12 and 14");
        d_payload << str;

        value = ais_value(ais, 143, 1);
        sprintf(str, "VHF Digital Selective Calling: %s\n",
                value == 0 ? "Not equipped":"Equipped with DSC function (dedicated or time-shared)");
        d_payload << str;

        value = ais_value(ais, 144, 1);
        sprintf(str, "VHF Band capability: %s\n",
                value == 0 ? "Capable of operating over the upper 525 kHz band of the marine band":"Capable of operating over the whole marine band");
        d_payload << str;

        value = ais_value(ais, 145, 1);
        sprintf(str, "Message 22 capability: %s\n",
                value == 0 ? "No frequency management via Message 22, operating on AIS1, AIS2 only":"Frequency management via Message 22");
        d_payload << str;

        value = ais_value(ais, 146, 1);
        sprintf(str, "Station Mode: %s\n",
                value == 0 ? "Station operating in autonomous and continuous mode":"Station operating in assigned mode");
        d_payload << str;

        print_raim(ais, 147, str);
    }
}

void ais_parse::decode_aid_to_navigation(unsigned char *ais, int len, char *str)
{
    if(len < 45) {
        if(d_verbose & V_DEBUG_2)
            printf("Erroneous report size %d bit, it should be >272 bit\n", len*6);

        if(!(d_verbose & V_DEBUG_3))
            return;
    }

    unsigned long v, v2;
    bool error = false;

    v = ais_value(ais, 38, 5);
    switch(v) {
    case  0: strcpy(str, "Not specified\n"); break;
    case  1: strcpy(str, "Reference point\n"); break;
    case  2: strcpy(str, "Radar transponder marking a navigation hazard\n"); break;
    case  3: strcpy(str, "Fixed structure off shore, such as oil platforms, wind farms, rigs, etc\n"); break;
    case  5: strcpy(str, "Light, without sectors\n"); break;
    case  6: strcpy(str, "Light, with sectors\n"); break;
    case  7: strcpy(str, "Leading Light Front\n"); break;
    case  8: strcpy(str, "Leading Light Rear\n"); break;
    case  9: strcpy(str, "Beacon, Cardinal North\n"); break;
    case 10: strcpy(str, "Beacon, Cardinal East\n"); break;
    case 11: strcpy(str, "Beacon, Cardinal South\n"); break;
    case 12: strcpy(str, "Beacon, Cardinal West\n"); break;
    case 13: strcpy(str, "Beacon, Port hand\n"); break;
    case 14: strcpy(str, "Beacon, Starboard hand\n"); break;
    case 15: strcpy(str, "Beacon, Preferred Channel port hand\n"); break;
    case 16: strcpy(str, "Beacon, Preferred Channel starboard hand\n"); break;
    case 17: strcpy(str, "Beacon, Isolated danger\n"); break;
    case 18: strcpy(str, "Beacon, Safe water\n"); break;
    case 19: strcpy(str, "Beacon, Special mark\n"); break;
    case 20: strcpy(str, "Cardinal Mark North\n"); break;
    case 21: strcpy(str, "Cardinal Mark East\n"); break;
    case 22: strcpy(str, "Cardinal Mark South\n"); break;
    case 23: strcpy(str, "Cardinal Mark West\n"); break;
    case 24: strcpy(str, "Port hand Mark\n"); break;
    case 25: strcpy(str, "Starboard hand Mark\n"); break;
    case 26: strcpy(str, "Preferred Channel Port hand\n"); break;
    case 27: strcpy(str, "Preferred Channel Starboard hand\n"); break;
    case 28: strcpy(str, "Isolated danger\n"); break;
    case 29: strcpy(str, "Safe Water\n"); break;
    case 30: strcpy(str, "Special Mark\n"); break;
    case 31: strcpy(str, "Light Vessel / LANBY / Rigs\n"); break;

    default:
        error = true;
    }

    if(!error)
        d_payload << "Navigation Aid Type: " << str;

    d_payload << "Ship Name: " << get_ais_text(ais, 43, 20, str) << "\n";
    print_position(ais, 164, str, "vessel");
    print_ship_dimension(ais, 219, str);
    print_position_fix_type(ais, 249, str);

    /*
       The Off-Position Indicator is for floating Aids-to-Navigation only:
       0 means on position; 1 means off position. Only valid if UTC second is equal to or below 59.
     */
    v = ais_value(ais, 253, 6);
    if(v < 60) {
        sprintf(str, "UTC Second: %d\n", v);
        d_payload << str;

        v = ais_value(ais, 259, 1);
        sprintf(str, "Off-Position Indicator: %s position\n", v == 0 ? "On":"Off");
        d_payload << str;
    }

    print_raim(ais, 268, str);

    /*
      The Virtual Aid flag is interpreted as follows: 0 = default = real Aid to Navigation at indicated position;
      1 = virtual Aid to Navigation simulated by nearby AIS station.
     */
    v = ais_value(ais, 269, 1);
    sprintf(str, "Virtual Aid to Navigation: %s\n",
            v == 0 ? "Real Aid to Navigation at indicated position":"Virtual Aid to Navigation simulated by nearby AIS station");
    d_payload << str;
}

void ais_parse::decode_static_data_msg(unsigned char *ais, int len, char *str)
{
    if(len != 26 || len != 28) {
        if(d_verbose & V_DEBUG_2)
            printf("Erroneous report size %d bit, it should be (Class A) 160 or (Class B) 168 bit\n", len*6);

        if(!(d_verbose & V_DEBUG_3))
            return;
    }

    unsigned long v;

    int part = ais_value(ais, 38, 2) & 0x01; // class A = 0, class B = 1

    if(part == 0)
        d_payload << "Ship Name: " << get_ais_text(ais, 40, 20, str) << "\n";
    else {
        print_ship_type(ais, 40, str);
        d_payload << "Vendor ID: " << get_ais_text(ais, 48, 7, str) << "\n";
        d_payload << "Call Sign: " << get_ais_text(ais, 90, 7, str) << "\n";
        print_ship_dimension(ais, 132, str);

        sprintf(str, "Mothership MMSI: %d\n", ais_value(ais, 132, 30));
        d_payload << str;
    }
}

void ais_parse::decode_long_range_msg(unsigned char *ais, int len, char *str)
{
    if(len != 16) {
        if(d_verbose & V_DEBUG_2)
            printf("Erroneous report size %d bit, it should be 96 bit\n", len*6);

        if(!(d_verbose & V_DEBUG_3))
            return;
    }

    print_raim(ais, 39, str);
    print_navigation_status(ais, 40, str);
    print_position(ais, 44, str, "vessel");

    unsigned long v = ais_value(ais, 79, 6);
    if(v < 63) {
        sprintf(str, "Speed Over Ground: %d knots\n");
        d_payload << str;
    }

    print_course_over_ground(ais, 85, str);

    v = ais_value(ais, 94, 1);
    sprintf(str, "GNSS Position status: %s GNSS position\n", v == 0 ? "Current":"Not");
    d_payload << str;
}

void ais_parse::print_navigation_status(unsigned char *ais, int bit_pos, char *str)
{
    int  value = ais_value(ais, bit_pos, 4);
    bool error = false;

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

    default:
        error = true;
    }

    if(!error)
        d_payload << str;
}

void ais_parse::print_ship_type(unsigned char *ais, int bit_pos, char *str)
{
    bool error = false;
    int v1 = ais_value(ais, bit_pos, 8);

    switch(v1) {
    case 20: strcpy(str, "Wing in ground (WIG)\n"); break;
    case 21:
    case 22:
    case 23:
    case 24: sprintf(str, "Wing in ground (WIG). Hazardous category %c (%d)\n", 68 - (24 - v1), v1); break;
    case 30: strcpy(str, "Fishing\n"); break;
    case 31: strcpy(str, "Towing\n"); break;
    case 32: strcpy(str, "Towing: length exceeds 200m or breadth exceeds 25m\n"); break;
    case 33: strcpy(str, "Dredging or underwater ops\n"); break;
    case 34: strcpy(str, "Diving ops\n"); break;
    case 35: strcpy(str, "Military ops\n"); break;
    case 36: strcpy(str, "Sailing\n"); break;
    case 37: strcpy(str, "Pleasure Craft\n"); break;
    case 40: strcpy(str, "High speed craft (HSC)\n"); break;
    case 41:
    case 42:
    case 43:
    case 44: sprintf(str, "High speed craft (HSC). Hazardous category %c (%d)\n", 68 - (44 - v1), v1); break;
    case 49: strcpy(str, "High speed craft (HSC)\n"); break;
    case 50: strcpy(str, "Pilot vessel\n"); break;
    case 51: strcpy(str, "Search and Rescue vessel\n"); break;
    case 52: strcpy(str, "Tug\n"); break;
    case 53: strcpy(str, "Port Tender (vessel anchored off shore)\n"); break;
    case 54: strcpy(str, "Anti-pollution equipment\n"); break;
    case 55: strcpy(str, "Law Enforcement\n"); break;
    case 58: strcpy(str, "Medical Transport\n"); break;
    case 59: strcpy(str, "Noncombatant ship according to RR Resolution No. 18\n"); break;
    case 60: strcpy(str, "Passenger\n"); break;
    case 61:
    case 62:
    case 63:
    case 64: sprintf(str, "Passenger. Hazardous category %c (%d)\n", 68 - (64 - v1), v1); break;
    case 69: strcpy(str, "Passenger\n"); break;
    case 70: strcpy(str, "Cargo\n"); break;
    case 71:
    case 72:
    case 73:
    case 74: sprintf(str, "Cargo. Hazardous category %c (%d)\n", 68 - (74 - v1), v1); break;
    case 79: strcpy(str, "Cargo\n"); break;
    case 80: strcpy(str, "Tanker\n"); break;
    case 81:
    case 82:
    case 83:
    case 84: sprintf(str, "Tanker. Hazardous category %c (%d)\n", 68 - (84 - v1), v1); break;
    case 89: strcpy(str, "Tanker\n"); break;
    case 90: strcpy(str, "Other Type\n"); break;
    case 91:
    case 92:
    case 93:
    case 94: sprintf(str, "Other Type. Hazardous category %c (%d)\n", 68 - (84 - v1), v1); break;
    case 99: strcpy(str, "Other Type\n"); break;

    default:
        error = true;
    }

    if(!error)
        d_payload << "Ship Type: " << str;
}

void ais_parse::print_raim(unsigned char *ais, int bit_pos, char *str)
{
    int i = ais_value(ais, bit_pos, 1);

    sprintf(str, "Receiver Autonomous Integrity Monitoring: %s\n",
             i == 0 ? "RAIM not in use":"RAIM in use");
    d_payload << str;
}

void ais_parse::decode_base_station(unsigned char *ais, int len, char *str)
{
    if((len*6) != 168) {
        if(d_verbose & V_DEBUG_2)
            printf("Erroneous report size %d bit, it should be 168 bit\n", len*6);

        if(!(d_verbose & V_DEBUG_3))
            return;
    }

    print_position_fix_type(ais, 134, str);

    sprintf(str, "%d-%02d-%02d %02d:%02d:%02d UTC\n",
            ais_value(ais, 38, 14),
            ais_value(ais, 52, 4),
            ais_value(ais, 56, 5),
            ais_value(ais, 61, 5), ais[11], ais[12]);
    d_payload << str;

    print_position(ais, 79, str, "station");

    if(ais[26] == 0 && ais[27] != 0) {
        sprintf(str, "%d vessels in range\n", ais[27]);
        d_payload << str;
    }
}

void ais_parse::decode_utc_inquiry(unsigned char *ais, int len, char *str)
{
    if(len != 12) {
        if(d_verbose & V_DEBUG_2)
            printf("Erroneous report size %d bit, it should be 72 bit\n", len*6);

        if(!(d_verbose & V_DEBUG_3))
            return;
    }

    sprintf(str, "Destination MMSI: %d\n", ais_value(ais, 40, 30));
    d_payload << str;
}

void ais_parse::decode_addr_ack_safety_msg(unsigned char *ais, int len, char *str)
{
    if(len <= 12) {
        if(d_verbose & V_DEBUG_2)
            printf("Erroneous report size %d bit, it should be >72 bit\n", len*6);

        if(!(d_verbose & V_DEBUG_3))
            return;
    }

    sprintf(str, "Sequence Number: %d\n", ais_value(ais, 38, 2)); d_payload << str;
    sprintf(str, "Destination MMSI: %d\n", ais_value(ais, 40, 30)); d_payload << str;
    d_payload << "Message: " << get_ais_text(ais, 72, len - 20, str) << "\n";
}

void ais_parse::decode_safety_broadcast_msg(unsigned char *ais, int len, char *str)
{
    if(len < 7) {
        if(d_verbose & V_DEBUG_2)
            printf("Erroneous report size %d bit, it should be >42 bit\n", len*6);

        if(!(d_verbose & V_DEBUG_3))
            return;
    }

    // I wonder if lenght should be in bits? 40/6 = 6.667 -> 7 byte
    d_payload << "Message: " << get_ais_text(ais, 40, len - 7, str) << "\n";
}

void ais_parse::decode_interrogation(unsigned char *ais, int len, char *str)
{
    if(len < 14) {
        if(d_verbose & V_DEBUG_2)
            printf("Erroneous report size %d bit, it should be 88-160 bit\n", len*6);

        if(!(d_verbose & V_DEBUG_3))
            return;
    }

    sprintf(str, "Interrogated MMSI: %d\n",   ais_value(ais,  40, 30)); d_payload << str;
    sprintf(str, "First message type: %d\n",  ais_value(ais,  70,  6)); d_payload << str;
    sprintf(str, "First slot offset: %d\n",   ais_value(ais,  76, 12)); d_payload << str;
    sprintf(str, "Second message type: %d\n", ais_value(ais,  90,  6)); d_payload << str;
    sprintf(str, "Second slot offset: %d\n",  ais_value(ais,  96, 12)); d_payload << str;
    sprintf(str, "Interrogated MMSI: %d\n",   ais_value(ais, 110, 30)); d_payload << str;
    sprintf(str, "First message type: %d\n",  ais_value(ais, 140,  6)); d_payload << str;
    sprintf(str, "First slot offset: %d\n",   ais_value(ais, 146, 12)); d_payload << str;
}

void ais_parse::decode_assignment_mode_command(unsigned char *ais, int len, char *str)
{
    if(!(len == 16 || len == 24)) {
        if(d_verbose & V_DEBUG_2)
            printf("Erroneous report size %d bit, it should be 96 or 144 bit\n", len*6);

        if(!(d_verbose & V_DEBUG_3))
            return;
    }

    sprintf(str, "Destination A MMSI: %d\n", ais_value(ais, 40, 30)); d_payload << str;
    sprintf(str, "Offset A: %d\n",           ais_value(ais, 70, 12)); d_payload << str;
    sprintf(str, "Increment A: %d\n",        ais_value(ais, 82, 10)); d_payload << str;

    if(len == 24) {
        sprintf(str, "Destination B MMSI: %d\n", ais_value(ais,  92, 30)); d_payload << str;
        sprintf(str, "Offset B: %d\n",           ais_value(ais, 122, 12)); d_payload << str;
        sprintf(str, "Increment B: %d\n",        ais_value(ais, 134, 10)); d_payload << str;
    }
}

void ais_parse::decode_dgnss_broadcast_bin_msg(unsigned char *ais, int len, char *str)
{
    if(len < 13) {
        if(d_verbose & V_DEBUG_2)
            printf("Erroneous report size %d bit, it should be 80-816 bit\n", len*6);

        if(!(d_verbose & V_DEBUG_3))
            return;
    }

#if 0

    // todo: I think print position display it incorrectly?
    int i, i2;

    i  = (int) ais_value(ais, 40, 18); // lon in minutes/10
    i2 = (int) ais_value(ais, 58, 17); // lat in minutes/10

    if(i < -180 || i > 180 || i2 < -90 || i2 > 90) {

    }
#endif

    print_position(ais, 40, str, "corrections for GPS");
    print_ais_payload_hex(ais, 80, len - 13, "DGNSS correction data (hex):\n");
}

void ais_parse::print_ais_payload_hex(unsigned char *ais, int bit_pos, int len, const char *data_desc)
{
    int i, hex;

    d_payload << data_desc;
    for(i=0; i<len; i++) {
        hex = (int) ais_value(ais, bit_pos + i*6, 6);

        if(i > 0) {
            if((i % 16) == 0 || (i - 1) == len)
                d_payload << "\n";
            else
                d_payload << " ";
        }

        d_payload << std::setw(2) << std::setfill('0') << std::hex << std::uppercase << int(hex) << "\n";
    }
}

void ais_parse::print_speed_over_ground(unsigned char *ais, int bit_pos, char *str, bool ship)
{
    int value = ais_value(ais, bit_pos, 10);
    double speed = ship ? (((double) value) / 10.0):value;

    if(value != 1023) {
        sprintf(str, "Speed Over Ground: %.*f %s",
                ship ? 1:0, speed,
                value == 1022 ? "or more\n":"knots\n");

        d_payload << str;
    }
}

void ais_parse::print_course_over_ground(unsigned char *ais, int bit_pos, char *str)
{
    unsigned int value = ais_value(ais, bit_pos, 12);

    if(value != 3600) {
        sprintf(str, "Course Over Ground: %.1f%s\n",
                ((double) value) / 10.0,
                DEG_SIGN);
        d_payload << str;
    }
}

void ais_parse::get_lonlat(unsigned char *ais, int bit_pos, double *lon, double *lat)
{
    unsigned int v;

    v = ais_value(ais, bit_pos, 28);
    *lon = ((double) v) / 600000.0;

    v = ais_value(ais, bit_pos + 28, 27);
    *lat = ((double) v) / 600000.0;
}

void ais_parse::print_position(unsigned char *ais, int bit_pos, char *str, const char *obj_type)
{
    double lon, lat;

    get_lonlat(ais, bit_pos, &lon, &lat);

    if(lon < -180 || lon > 180 || lat < -90 || lat > 90) {
        if(d_verbose & V_DEBUG_2)
            printf("Erroneous latitude: %f or longitude: %f\n", lat, lon);

        if(!(d_verbose & V_DEBUG_3))
            return;
    }

    double dist, az, s;
    int d, m;

    toDMS(lon, &d, &m, &s);
    sprintf(str, "Longitude: %c %d%s %d' %.3f\" (%.6f%s)\n",
            lon < 0 ? 'W':'E',
            d, DEG_SIGN, m, s,
            lon, DEG_SIGN);
    d_payload << str;

    toDMS(lat, &d, &m, &s);
    sprintf(str, "Latitude : %c %d%s %d' %.3f\" (%.6f%s)\n",
            lat < 0 ? 'S':'N',
            d, DEG_SIGN, m, s,
            lat, DEG_SIGN);
    d_payload << str;

    dist = wgs84distance(d_qth_lon, d_qth_lat, lon, lat);
    az   = wgs84bearing(d_qth_lon, d_qth_lat, lon, lat);

    sprintf(str, "Distance %.3f M (%.*f %s) and bearing %.1f%s to %s\n",
            dist / 1851.85,
            dist > 10000 ? 3:0,
            dist > 10000 ? (dist/1000.0):dist,
            dist > 10000 ? "km":"m",
            az, DEG_SIGN,
            obj_type);

    d_payload << str;
}

void ais_parse::toDMS(double ll, int *d, int *m, double *s)
{
    double dm;

    *d = (int) fabs(ll);
    dm = fabs(ll - *d) * 60.0;
    *m = (int) dm;
    *s = fabs(dm - *m) * 60.0;
}

void ais_parse::print_position_fix_type(unsigned char *ais, int bit_pos, char *str)
{
    int type = ais_value(ais, bit_pos, 4) & 0x0f;
    bool error = false;

    switch(type) {
    case 1: strcpy(str, "Station Electronic Position Fixing Device: GPS\n"); break;
    case 2: strcpy(str, "Station Electronic Position Fixing Device: GLONASS\n"); break;
    case 3: strcpy(str, "Station Electronic Position Fixing Device: Combined GPS/GLONASS\n"); break;
    case 4: strcpy(str, "Station Electronic Position Fixing Device: Loran-C\n"); break;
    case 5: strcpy(str, "Station Electronic Position Fixing Device: Chayka\n"); break;
    case 6: strcpy(str, "Station Electronic Position Fixing Device: Integrated navigation system\n"); break;
    case 7: strcpy(str, "Station Electronic Position Fixing Device: Surveyed\n"); break;
    case 8: strcpy(str, "Station Electronic Position Fixing Device: Galileo\n"); break;

    default:
        error = true;
    }

    if(!error)
        d_payload << str;
}

void ais_parse::print_ship_properties(unsigned char *ais, int bit_pos, char *str)
{
    int bp = bit_pos;
    d_payload << "Ship Name: " << get_ais_text(ais, bp, 20, str) << "\n";
    bp += 20;

    print_ship_type(ais, bp, str);
    bp += 8;

    print_ship_dimension(ais, bp, str);
    bp += 9;
}

void ais_parse::print_ship_dimension(unsigned char *ais, int bit_pos, char *str)
{
    int bp = bit_pos;
    int v1 = ais_value(ais, bp, 9);
    bp += 9;
    if(v1) {
        sprintf(str, "Dimension to Bow: %d m%s", v1, v1 == 511 ? " or greater\n":"\n");
        d_payload << str;
    }

    v1 = ais_value(ais, bp, 9);
    bp += 9;
    if(v1) {
        sprintf(str, "Dimension to Stern: %d m%s", v1, v1 == 511 ? " or greater\n":"\n");
        d_payload << str;
    }

    v1 = ais_value(ais, bp, 6);
    bp += 6;
    if(v1) {
        sprintf(str, "Dimension to Port: %d m%s", v1, v1 == 63 ? " or greater\n":"\n");
        d_payload << str;
    }
    v1 = ais_value(ais, bp, 6);
    bp += 6;
    if(v1) {
        sprintf(str, "Dimension to Starboard: %d m%s", v1, v1 == 63 ? " or greater\n":"\n");
        d_payload << str;
    }
}

unsigned long ais_parse::ais_value(unsigned char *ais, int bit_pos, int len)
{
    unsigned long rc, mask;
    unsigned char c;
    int i;

    int byte = bit_pos / 6;
    int bits = bit_pos % 6;

    // len is in bits

    mask = 0;
    for(i=0; i<len; i++)
        mask |= 1 << i;

    c = 0;
    for(i=(6 - bits); i<6; i++)
        c |= 1 << i;

    len -= (6 - bits);
    rc = (ais[byte] & (c ^ 0x3f)) << len;

    if(len == 0)
        return (rc & mask);

    byte += 1;
    while(len >= 6) {
        rc |= ais[byte++] << (len - 6);
        len -= 6;
    }

    if(len == 0)
        return (rc & mask);

    bits = 6 - len;
    c = 0;
    for(i=0; i<bits; i++)
        c |= 1 << i;

    rc |= (ais[byte] & (c ^ 0x3f)) >> bits;

    return (rc & mask);
}

char *ais_parse::get_ais_text(unsigned char *ais, int bit_pos, int len6, char *buf)
{
    unsigned char v;
    char ch, prev_ch = 'a';
    int i;

    *buf = '\0';

    if(len6 < 1)
        return buf;

    for(i=0; i<len6; i++) {
        v = ais_value(ais, bit_pos + i*6, 6);
        ch = ais_ascii_table[v & 0x3f];

#if 0
        if((prev_ch == 32 && ch == 32) || (prev_ch == '@' && ch == '@'))
#else
        // I wonder if the msg should stop at ais ascii char 0 -> @, ie null (0)?
        if(ch == '@' || (prev_ch == 32 && ch == 32))
#endif
            break;

        buf[i] = ch;
        prev_ch = ch;
    }

    buf[i > 0 ? (i-1):0] = '\0';

    return buf;
}

double ais_parse::wgs84distance(double lon1, double lat1, double lon2, double lat2)
{
    if(lon1 == lon2 && lat1 == lat2)
        return 0;

    double dLon, U1, U2, sinU1, cosU1, sinU2, cosU2;
    double lam, lamPrev, sinLam, cosLam, cos2sigM;
    double sinSig, cosSig, sig, sinAlpha, cosAlpha2;
    double C, Cb2, u2, A, B, dsig;
    double ea, eb, ef;
    int i;

    // WGS84 ellipsoid
    ea = 6378137.0;
    eb = 6356752.314245;
    ef = 1.0 / 298.257223563;

    dLon = (lon2 - lon1) * DTR;

    U1 = atan((1.0 - ef) * tan(lat1 * DTR));
    U2 = atan((1.0 - ef) * tan(lat2 * DTR));
    sinU1 = sin(U1);
    cosU1 = cos(U1);
    sinU2 = sin(U2);
    cosU2 = cos(U2);

    lam = dLon;
    i = 0;

    while((i++ == 0 || fabs(lam - lamPrev) > 1e-12) && i < 20)
    {
        lamPrev = lam;
        sinLam = sin(lam);
        cosLam = cos(lam);

        sinSig = sqrt(pow(cosU2 * sinLam, 2.0) + pow(cosU1 * sinU2 - sinU1 * cosU2 * cosLam, 2.0));
        if(sinSig == 0.0)
            return 0; // co-incident points
        cosSig = sinU1 * sinU2 + cosU1 * cosU2 * cosLam;
        sig = atan2(sinSig, cosSig);
        sinAlpha = cosU1 * cosU2 * sinLam / sinSig;
        cosAlpha2 = 1.0 - pow(sinAlpha, 2.0);

        if(cosAlpha2 == 0.0)
            cos2sigM = 0;
        else
            cos2sigM = cosSig - 2.0 * sinU1 * sinU2 / cosAlpha2;

        C = ef / 16.0 * cosAlpha2 * (4.0 + ef * (4.0 - 3.0 * cosAlpha2));

        lam = dLon + (1.0 - C) * ef * sinAlpha * (sig + C * sinSig * (cos2sigM + C * cosSig * (-1.0 + 2.0 * pow(cos2sigM, 2.0))));
    }

    Cb2 = pow(eb, 2.0);
    u2 = cosAlpha2 * (pow(ea, 2.0) - Cb2) / Cb2;
    A = 1.0 + u2 / 16384.0 * (4096.0 + u2 * (-768.0 + u2 * (320.0 - 175.0 * u2)));
    B = u2 / 1024.0 * (256.0 + u2 * (-128.0 + u2 * (74.0 - 47.0 * u2)));
    dsig = B * sinSig * (cos2sigM + 0.25 * B * (cosSig * (-1.0 + 2.0 * pow(cos2sigM, 2.0)) -
           1.0 / 6.0 * B * cos2sigM * (-3.0 + 4.0 * pow(sinSig, 2.0)) * (-3.0 + 4.0 * pow(cos2sigM, 2.0))));

    return (eb * A * (sig - dsig)); // meter
}

double ais_parse::wgs84bearing(double lon1, double lat1, double lon2, double lat2)
{
    if(lon1 == lon2 && lat1 == lat2)
        return 0;

    double dLon, U1, U2, sinU1, cosU1, sinU2, cosU2;
    double lam, lamPrev, sinLam, cosLam, cos2sigM;
    double sinSig, cosSig, sig, sinAlpha, cosAlpha2;
    double C, Cb2, u2, A, B, dsig, ef;
    int i;

    // WGS84 ellipsoid
    ef = 1.0 / 298.257223563;

    dLon = (lon2 - lon1) * DTR;

    U1 = atan((1.0 - ef) * tan(lat1 * DTR));
    U2 = atan((1.0 - ef) * tan(lat2 * DTR));
    sinU1 = sin(U1);
    cosU1 = cos(U1);
    sinU2 = sin(U2);
    cosU2 = cos(U2);

    lam = dLon;
    i = 0;

    while((i++ == 0 || fabs(lam - lamPrev) > 1e-12) && i < 20)
    {
        lamPrev = lam;
        sinLam = sin(lam);
        cosLam = cos(lam);

        sinSig = sqrt(pow(cosU2 * sinLam, 2.0) + pow(cosU1 * sinU2 - sinU1 * cosU2 * cosLam, 2.0));
        if(sinSig == 0.0)
            return 0; // co-incident points
        cosSig = sinU1 * sinU2 + cosU1 * cosU2 * cosLam;
        sig = atan2(sinSig, cosSig);
        sinAlpha = cosU1 * cosU2 * sinLam / sinSig;
        cosAlpha2 = 1.0 - pow(sinAlpha, 2.0);

        if(cosAlpha2 == 0.0)
            cos2sigM = 0;
        else
            cos2sigM = cosSig - 2.0 * sinU1 * sinU2 / cosAlpha2;

        C = ef / 16.0 * cosAlpha2 * (4.0 + ef * (4.0 - 3.0 * cosAlpha2));

        lam = dLon + (1.0 - C) * ef * sinAlpha * (sig + C * sinSig * (cos2sigM + C * cosSig * (-1.0 + 2.0 * pow(cos2sigM, 2.0))));
    }

    // from lon2, lat2 -> lon1, lat1 (target <- you)
    double az1 = (atan2(cosU2 * sinLam, cosU2 * sinU2 - sinU1 * cosU2 * cosLam)) * RTD;
    if(d_verbose & V_DEBUG_6)
        printf("\naz1: %f\n", az1);

    while(az1 < 0)
        az1 += 360.0;

    while(az1 > 360)
        az1 -= 360.0;

    // you -> target
    double az2 = (atan2(cosU1 * sinLam, -sinU1 * cosU2 + cosU1 * sinU2 * cosLam)) * RTD;
    if(d_verbose & V_DEBUG_6)
        printf("az2: %f\n", az2);

    while(az2 < 0)
        az2 += 360.0;

    while(az2 > 360)
        az2 -= 360.0;

    return az2;
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
    unsigned short rc;
    int i, j;

    int datalen = len/8;

    char data[256];
    for(j=0; j<datalen; j++) // this unpacks the data in preparation for calculating CRC
        data[j] = unpack(buffer, j*8, 8);

    for(i = 0;  i < datalen;  i++)
        crc = (crc >> 8) ^ crc_itu16_table[(crc ^ data[i]) & 0xFF];

    rc = (crc & 0xFFFF) != 0xF0B8;

    return rc;
}

unsigned char ais_parse::packet_crc(const char *buffer)
{
    // ah, same as nmea_checksum...
    size_t i, len = strlen(buffer);
    unsigned char crc = 0;

    printf("crc chk: %s\n", buffer);
    for(i=1; i<len; i++) {
        if(buffer[i] == '*')
            break;

        crc ^= buffer[i];
    }

    return crc;
}
