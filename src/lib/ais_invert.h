//ais_invert.h
/* -*- c++ -*- */
/*
 * Copyright 2004 Free Software Foundation, Inc.
 * 
 * This file is part of GNU Radio
 * 
 * GNU Radio is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
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
#ifndef AIS_INVERT_H
#define AIS_INVERT_H

#include <gr_sync_block.h>

class ais_invert;

/*
 * We use boost::shared_ptr's instead of raw pointers for all access
 * to gr_blocks (and many other data structures).  The shared_ptr gets
 * us transparent reference counting, which greatly simplifies storage
 * management issues.  This is especially helpful in our hybrid
 * C++ / Python system.
 *
 * See http://www.boost.org/libs/smart_ptr/smart_ptr.htm
 *
 * As a convention, the _sptr suffix indicates a boost::shared_ptr
 */
typedef boost::shared_ptr<ais_invert> ais_invert_sptr;

/*!
 * \brief Return a shared_ptr to a new instance of ais_invert.
 *
 * To avoid accidental use of raw pointers, ais_invert's
 * constructor is private.  ais_make_invert is the public
 * interface for creating new instances.
 */
ais_invert_sptr ais_make_invert();

/*!
 * \brief invert a packed stream of bits.
 * \ingroup block
 *
 *
 * This uses the preferred technique: subclassing gr_sync_block.
 */
class ais_invert : public gr_sync_block
{
private:
  // The friend declaration allows ais_make_invert to
  // access the private constructor.

  friend ais_invert_sptr ais_make_invert();

  ais_invert();   // private constructor

 public:
  ~ais_invert();  // public destructor

  // Where all the action really happens

  int work (int noutput_items,
            gr_vector_const_void_star &input_items,
            gr_vector_void_star &output_items);
};

#endif /* AIS_INVERT_H */

