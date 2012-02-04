//ais_unstuff.h
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
#ifndef AIS_unstuff_H
#define AIS_unstuff_H

#include <gr_block.h>

class ais_unstuff;

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
typedef boost::shared_ptr<ais_unstuff> ais_unstuff_sptr;

/*!
 * \brief Return a shared_ptr to a new instance of ais_unstuff.
 *
 * To avoid accidental use of raw pointers, ais_unstuff's
 * constructor is private.  ais_make_unstuff is the public
 * interface for creating new instances.
 */
ais_unstuff_sptr ais_make_unstuff();

/*!
 * \brief unstuff a packed stream of bits.
 * \ingroup block
 *
 *
 * This uses the preferred technique: subclassing gr_block.
 */
class ais_unstuff : public gr_block
{
private:
  // The friend declaration allows ais_make_unstuff to
  // access the private constructor.

  friend ais_unstuff_sptr ais_make_unstuff();

  ais_unstuff();   // private constructor

  int d_consecutive;

 public:
  ~ais_unstuff();  // public destructor

  // Where all the action really happens

  int general_work (int noutput_items,
            gr_vector_int &ninput_items,
            gr_vector_const_void_star &input_items,
            gr_vector_void_star &output_items);

  void forecast (int noutput_items,
	       gr_vector_int &ninput_items_required);
};

#endif /* AIS_unstuff_H */

