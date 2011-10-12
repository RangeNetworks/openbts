/*
* Copyright 2011 Free Software Foundation, Inc.
*
*
* This software is distributed under the terms of the GNU Affero Public License.
* See the COPYING file in the main directory for details.
*
* This use of this software may be subject to additional restrictions.
* See the LEGAL file in the main directory for details.

	This program is free software: you can redistribute it and/or modify
	it under the terms of the GNU Affero General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU Affero General Public License for more details.

	You should have received a copy of the GNU Affero General Public License
	along with this program.  If not, see <http://www.gnu.org/licenses/>.

*/

#ifndef _FUSB_H_
#define _FUSB_H_

//#include "libusb_types.h"
#include <list>
#include <libusb-1.0/libusb.h>
#include "Logger.h"

struct libusb_device;
struct libusb_device_handle;
struct libusb_device_descriptor;

struct  libusb_transfer;
struct 	libusb_context;
class   fusb_ephandle;

/*!
 * \brief abstract usb device handle
 */
class fusb_devhandle {

private:
  std::list<libusb_transfer*>    d_pending_rqsts;
  libusb_context                *d_ctx;

  void pending_add (struct libusb_transfer *lut);
  struct libusb_transfer * pending_get ();

  bool d_teardown;

public:
  fusb_devhandle (libusb_device_handle *udh, libusb_context *ctx);
  ~fusb_devhandle ();

  fusb_ephandle *make_ephandle (int endpoint, bool input_p,
                                        int block_size = 0, int nblocks = 0);
  bool _submit_lut (libusb_transfer *);
  bool _reap (bool ok_to_block_p);

  bool pending_remove (struct libusb_transfer *lut);
  inline bool _teardown() { return d_teardown; }

protected:
  libusb_device_handle		*d_udh;

public:
  fusb_devhandle (libusb_device_handle *udh) {d_udh = udh;}
  libusb_device_handle *get_usb_dev_handle () const { return d_udh; }
};


/*!
 * \brief abstract usb end point handle
 */
class fusb_ephandle {
private:

  // NOT IMPLEMENTED
  fusb_ephandle (const fusb_ephandle &rhs);	        // no copy constructor
  fusb_ephandle &operator= (const fusb_ephandle &rhs);  // no assignment operator


private:
  fusb_devhandle         *d_devhandle;
  std::list<libusb_transfer*>     d_free_list;
  std::list<libusb_transfer*>     d_completed_list;
  libusb_transfer                *d_write_work_in_progress;
  unsigned char                  *d_write_buffer;
  libusb_transfer                *d_read_work_in_progress;
  unsigned char                  *d_read_buffer;
  unsigned char                  *d_read_buffer_end;

  libusb_transfer *get_write_work_in_progress ();
  void reap_complete_writes ();
  bool reload_read_buffer ();
  bool submit_lut (libusb_transfer *lut);


protected:
  int				d_endpoint;
  bool				d_input_p;
  int				d_block_size;
  int				d_nblocks;
  bool				d_started;

public:
  fusb_ephandle (fusb_devhandle *dh, int endpoint, bool input_p,
                         int block_size = 0, int nblocks = 0);
  ~fusb_ephandle();

  bool start ();        //!< begin streaming i/o

  int write (const void *buffer, int nbytes);

  int read (void *buffer, int nbytes);

  void free_list_add (struct libusb_transfer *lut);
  void completed_list_add (struct libusb_transfer *lut);
  struct libusb_transfer *free_list_get ();
  struct libusb_transfer *completed_list_get ();

  // accessor to work from callback context
  fusb_devhandle* get_fusb_devhandle () const {
    return d_devhandle;
  }


public:
  fusb_ephandle (int endpoint, bool input_p,
		 int block_size = 0, int nblocks = 0);

  int block_size () { return d_block_size; };
};

static const int MAX_BLOCK_SIZE = 16 * 1024;            // hard limit
static const int DEFAULT_BLOCK_SIZE =   4 * 1024;
static const int DEFAULT_BUFFER_SIZE = 4 * (1L << 20);     // 1 MB

/*!
 * \brief factory for creating concrete instances of the appropriate subtype.
 */
class fusb {
public:
  static fusb_devhandle *make_devhandle (libusb_device_handle *udh,
                                         libusb_context *ctx = 0) 
  { return new fusb_devhandle (udh,ctx);}

  static int max_block_size () { return MAX_BLOCK_SIZE;}

  static int default_block_size () { return DEFAULT_BLOCK_SIZE;}

  static int default_buffer_size () { return DEFAULT_BUFFER_SIZE;}

};

#endif /* _FUSB_H_ */
