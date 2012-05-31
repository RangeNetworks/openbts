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

#include "rnrad1Core.h"

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fpga_regs.h"
#include <stdexcept>
#include <assert.h>
#include <math.h>
#include "ad9862.h"
#include <cstdio>

using namespace ad9862;

static const double POLLING_INTERVAL = 0.1;	// seconds

static const char *_get_usb_error_str (int usb_err)
{
  switch (usb_err) {
  case LIBUSB_SUCCESS: return "Success (no error)";
  case LIBUSB_ERROR_IO: return "Input/output error";
  case LIBUSB_ERROR_INVALID_PARAM: return "Invalid parameter";
  case LIBUSB_ERROR_ACCESS: return "Access denied (insufficient permissions)";
  case LIBUSB_ERROR_NO_DEVICE: return "No such device (it may have been disconnected)";
  case LIBUSB_ERROR_NOT_FOUND: return "Entity not found";
  case LIBUSB_ERROR_BUSY: return "Resource busy";
  case LIBUSB_ERROR_TIMEOUT: return "Operation timed out";
  case LIBUSB_ERROR_OVERFLOW: return "Overflow";
  case LIBUSB_ERROR_PIPE: return "Pipe error";
  case LIBUSB_ERROR_INTERRUPTED: return "System call interrupted (perhaps due to signal)";
  case LIBUSB_ERROR_NO_MEM: return "Insufficient memory";
  case LIBUSB_ERROR_NOT_SUPPORTED: return "Operation not supported or unimplemented on this platform";
  case LIBUSB_ERROR_OTHER: return "Unknown error";
  }

  return "Unknown error";
}

int usbMsg (struct libusb_device_handle *udh,
	       int request, int value, int index,
	       unsigned char *bytes, int len)
{
  int requesttype = (request & 0x80) ? VRT_VENDOR_IN : VRT_VENDOR_OUT;

  int ret = libusb_control_transfer(udh, requesttype, request, value, index,
                                    bytes, len, 1000);

  if (ret < 0) {
    // we get EPIPE if the firmware stalls the endpoint.
    if (ret != LIBUSB_ERROR_PIPE) {
      LOG(ERR) << "libusb_control_transfer failed: " << _get_usb_error_str(ret);
    }
  }

  return ret;
}


struct libusb_device_handle *rad1OpenInterface (libusb_device *dev, int interface, int altinterface)
{
  libusb_device_handle *udh;
  int ret;

  if (libusb_open (dev, &udh) < 0) return NULL;

  if (dev != libusb_get_device (udh)){
    LOG(ERR) << "Can't get USB device";
    exit(0);
  }
  
  if ((ret = libusb_claim_interface (udh, interface)) < 0) {
    LOG(ERR) << "Can't claim USB interface: " << interface << " bc: " << _get_usb_error_str(ret);
    libusb_close (udh);
    return NULL;
  }
  
  if ((ret = libusb_set_interface_alt_setting (udh, interface,
                                               altinterface)) < 0) {
    LOG(ERR) << "Can't set USB alt interface: " << altinterface << " bc: " << _get_usb_error_str(ret);
    libusb_release_interface (udh, interface);
    libusb_close (udh);
    return NULL;
  }
  
  return udh;
}


static const int FIRMWARE_HASH_SLOT     = 0;
static const int FPGA_HASH_SLOT         = 1;

static const int hashSlotAddr[2] = {
  RAD1_HASH_SLOT_0_ADDR,
  RAD1_HASH_SLOT_1_ADDR
};


bool rad1GetHash (libusb_device_handle *udh, int which,
	    unsigned char hash[RAD1_HASH_SIZE])
{
  which &= 1;

  // we use the Cypress firmware upload command to fetch it.
  int r = libusb_control_transfer (udh, 0xc0, 0xa0, hashSlotAddr[which], 0,
                                (unsigned char *) hash, RAD1_HASH_SIZE, 1000);

  if (r < 0) {
     LOG(ERR) << "Failed to get hash, USB err: " << _get_usb_error_str(r);
  }

  return r == RAD1_HASH_SIZE;
}

struct libusb_device *rad1FindDevice (int nth, bool allowFx2, libusb_context *ctx)
{
  libusb_device **list;

  struct libusb_device *q;
  int    nFound = 0;

  size_t cnt = libusb_get_device_list(ctx, &list);
  size_t i = 0;

  if (cnt < 0)
    LOG(ERR) << "libusb_get_device_list failed: " <<_get_usb_error_str(cnt);

  for (i = 0; i < cnt; i++) {
    q = list[i];
    libusb_device_descriptor desc;
    int ret = libusb_get_device_descriptor(q, &desc);
    if (ret < 0) {
      LOG(ERR) << "usb_get_device_descriptor failed: " <<_get_usb_error_str(ret);
    }

    if ((desc.idVendor == USB_VID_FSF && desc.idProduct == USB_PID_FSF_RAD1) ||
        (allowFx2 && desc.idVendor == USB_VID_CYPRESS && desc.idProduct == USB_PID_CYPRESS_FX2)) {
        if (nFound == nth)     // return this one
          return q;
        nFound++;              // keep looking
    }
  }

  // The list needs to be freed. Right now just release it if nothing is found.
  libusb_free_device_list(list, 1);

  return 0;     // not found
}

static libusb_device_handle *openNthCmdInterface (int nth, libusb_context *ctx)
{
  
  libusb_device *udev = rad1FindDevice (nth, false, ctx);
  if (udev == 0){
    LOG(ERR) << "Failed to find rad1 num " << nth;
    return 0;
  }

  libusb_device_handle *udh;

  udh = rad1OpenInterface (udev, RAD1_CMD_INTERFACE, RAD1_CMD_ALTINTERFACE);
  if (udh == 0){
    LOG(ERR) << "Can't open_cmd_interface";
    return 0;
  }

  return udh;
}

enum rad1LoadStatus { ULS_ERROR = 0, ULS_OK, ULS_ALREADY_LOADED };

bool rad1SetHash (libusb_device_handle *udh, int which,
		    const unsigned char hash[RAD1_HASH_SIZE])
{
  which &= 1;
  
  // we use the Cypress firmware down load command to jam it in.
  int r = libusb_control_transfer (udh, 0x40, 0xa0, hashSlotAddr[which], 0,
                                (unsigned char *) hash, RAD1_HASH_SIZE, 1000);

  if (r < 0) {
     LOG(ERR) << "Failed to set hash bc: " << _get_usb_error_str(r);
  }

  return r == RAD1_HASH_SIZE;
}

static bool writeRam (libusb_device_handle *udh, unsigned char *buf,
					int startAddr, size_t len)
{
  int addr;
  int n;
  int a;
  int quanta = MAX_EP0_PKTSIZE;

  for (addr = startAddr; addr < startAddr + (int) len; addr += quanta){
    n = len + startAddr - addr;
    if (n > quanta)
      n = quanta;

    a = libusb_control_transfer(udh, 0x40, 0xA0, addr, 0,
                       (unsigned char*)(buf + (addr - startAddr)), n, 1000);

    if (a < 0){
      LOG(ERR) << "Write to RAM failed bc: " << _get_usb_error_str(a);
      return false;
    }
  }
  return true;
}

// Load intel format file into cypress FX2 (8051)

bool rad1LoadFirmware (libusb_device_handle *udh, const char *filename,
				 unsigned char hash[RAD1_HASH_SIZE])
{
  FILE  *f = fopen (filename, "ra");
  if (f == 0){
    perror (filename);
    return false;
  }

  // hold CPU in reset while loading firmware
  unsigned char reset = 1;
  if (!writeRam (udh, &reset, 0xE600, 1))
    goto fail;


  char s[1024];
  int length;
  int addr;
  int type;
  unsigned char data[256];
  unsigned char checksum, a;
  unsigned int b;
  int i;

  while (!feof(f)){
    fgets(s, sizeof (s), f); /* we should not use more than 263 bytes normally */
    if(s[0]!=':'){
      LOG(ERR) "File " << filename << " has invalid line: " << s;
      goto fail;
    }
    sscanf(s+1, "%02x", &length);
    sscanf(s+3, "%04x", &addr);
    sscanf(s+7, "%02x", &type);
    
    if(type==0){
      a=length+(addr &0xff)+(addr>>8)+type;
      for(i=0;i<length;i++){
        sscanf (s+9+i*2,"%02x", &b);
        data[i]=b;
        a=a+data[i];
      }
      sscanf (s+9+length*2,"%02x", &b);
      checksum=b;
      if (((a+checksum)&0xff)!=0x00){
        LOG(ERR) << "Checksum failed: got " << (unsigned int) -a << " expected " << (unsigned int) checksum;
        goto fail;
      }
      if (!writeRam (udh, data, addr, length))
        goto fail;
    }
    else if (type == 0x01){      // EOF
      break;
    }
    else if (type == 0x02){
      LOG(ERR) << "Extended address: " << s;
      goto fail;
    }
  }

  // we jam the hash value into the FX2 memory before letting
  // the cpu out of reset.  When it comes out of reset it
  // may renumerate which will invalidate udh.

  if (!rad1SetHash (udh, FIRMWARE_HASH_SLOT, hash))
    LOG(ERR) << "Failed to write firmware hash";

  // take CPU out of reset
  reset = 0;
  if (!writeRam (udh, &reset, 0xE600, 1))
    goto fail;


  fclose (f);
  return true;

 fail:
  fclose (f);
  return false;
}

// ----------------------------------------------------------------
// load fpga

bool rad1LoadFpga (libusb_device_handle *udh, const char *filename,
			     unsigned char hash[RAD1_HASH_SIZE])
{
  bool ok = true;

  FILE  *fp = fopen (filename, "rb");
  if (fp == 0){
    perror (filename);
    return false;
  }

  unsigned char buf[MAX_EP0_PKTSIZE];   // 64 is max size of EP0 packet on FX2
  int n;

  usbMsg (udh, VRQ_SET_LED, 1, 1, 0, 0);

  // reset FPGA (and on rev1 both AD9862's, thus killing clock)
  usbMsg(udh, VRQ_FPGA_SET_RESET, 1, 0, 0, 0);         // hold fpga in reset

  if (usbMsg (udh, VRQ_FPGA_LOAD, 0, FL_BEGIN, 0, 0) != 0)
    goto fail;

  while ((n = fread (buf, 1, sizeof (buf), fp)) > 0){
    if (usbMsg (udh, VRQ_FPGA_LOAD, 0, FL_XFER, buf, n) != n)
      goto fail;
  }
  
  if (usbMsg (udh, VRQ_FPGA_LOAD, 0, FL_END, 0, 0) != 0)
    goto fail;

  fclose (fp);

  if (!rad1SetHash (udh, FPGA_HASH_SLOT, hash))
    LOG(ERR) << "Failed to write FPGA hash";

  // On the rev1 RAD1, the {tx,rx}_{enable,reset} bits are
  // controlled over the serial bus, and hence aren't observed until
  // we've got a good fpga bitstream loaded.
  usbMsg(udh, VRQ_FPGA_SET_RESET, 0, 0, 0, 0);         // fpga out of master reset

  // now these commands will work
  ok &= (usbMsg(udh, VRQ_FPGA_SET_TX_ENABLE, 0, 0, 0, 0)!=0);
  ok &= (usbMsg(udh, VRQ_FPGA_SET_RX_ENABLE, 0, 0, 0, 0)!=0);
  ok &= (usbMsg(udh, VRQ_FPGA_SET_TX_RESET , 1, 0, 0, 0)!=0);
  ok &= (usbMsg(udh, VRQ_FPGA_SET_RX_RESET , 1, 0, 0, 0)!=0);
  ok &= (usbMsg(udh, VRQ_FPGA_SET_TX_RESET , 0, 0, 0, 0)!=0);
  ok &= (usbMsg(udh, VRQ_FPGA_SET_RX_RESET , 0, 0, 0, 0)!=0);

  if (!ok)
    LOG(ERR) << "Failed to reset TX/RX path";

  // Manually reset all regs except master control to zero.
  // FIXME may want to remove this when we rework FPGA reset strategy.
  // In the mean while, this gets us reproducible behavior.
  // FIXME!!!
  
  for (int i = 0; i < FR_USER_0; i++){
    if (i == FR_MASTER_CTRL)
      continue;
    buf[0] = (0 >> 24) & 0xff;        // MSB first  
    buf[1] = (0 >> 16) & 0xff;
    buf[2] = (0 >>  8) & 0xff;
    buf[3] = (0 >>  0) & 0xff;

    usbMsg (udh, VRQ_SPI_WRITE,
                    0x00 | (i & 0x7f),
                    ((SPI_ENABLE_FPGA & 0xff) << 8) | ((SPI_FMT_MSB | SPI_FMT_HDR_1) & 0xff),
                    buf, 4);
    //rad1writeFpgaReg(udh, i, 0);
  }
  

  usbMsg (udh, VRQ_SET_LED, 1, 0, 0, 0); // led 1 off

  return true;

 fail:
  fclose (fp);
  return false;
}

bool computeHash (const char *filename, unsigned char hash[RAD1_HASH_SIZE])
{
  assert (RAD1_HASH_SIZE == 16);
  memset (hash, 0, RAD1_HASH_SIZE);

  FILE *fp = fopen (filename, "rb");
  if (fp == 0){
    perror (filename);
    return false;
  }
  int i = 0;
  while (!feof(fp)) {getc(fp);i++;}
  sprintf((char *)hash,"%d",i);
  //int r = md5_stream (fp, (void*) hash);
  fclose (fp);

  return true;
}

rad1LoadStatus rad1LoadFirmwareNth (int nth, const char *filename, bool force, libusb_context *ctx)
{
  libusb_device_handle *udh = openNthCmdInterface (nth, ctx);
  if (udh == 0)
    return ULS_ERROR;

  rad1LoadStatus s;

  unsigned char file_hash[RAD1_HASH_SIZE];
  unsigned char rad1_hash[RAD1_HASH_SIZE];

  if (access (filename, R_OK) != 0){
    perror (filename);
    s = ULS_ERROR;
  }
  else if (!computeHash (filename, file_hash))
    s =  ULS_ERROR;
  else if (!force
	   && rad1GetHash (udh, FIRMWARE_HASH_SLOT, rad1_hash)
	   && memcmp (file_hash, rad1_hash, RAD1_HASH_SIZE) == 0) {
    //printf("hash: %s %s\n",file_hash,rad1_hash);
    s = ULS_ALREADY_LOADED;
  }
  else if (!rad1LoadFirmware(udh, filename, file_hash))
    s = ULS_ERROR;
  else
    s = ULS_OK;

  libusb_close(udh);

  switch (s){

  case ULS_ALREADY_LOADED:              // nothing changed...
    return ULS_ALREADY_LOADED;
    break;

  case ULS_OK:
    // we loaded firmware successfully.

    // It's highly likely that the board will renumerate (simulate a
    // disconnect/reconnect sequence), invalidating our current
    // handle.

    // FIXME.  Turn this into a loop that rescans until we refind ourselves

    sleep(2);

    return ULS_OK;

  default:
  case ULS_ERROR:               // some kind of problem
    return ULS_ERROR;
  }
}

static void load_status_msg (rad1LoadStatus s, const char *type, const char *filename)
{
  switch (s){
  case ULS_ERROR:
    LOG(ERR) << "Failed to load " << type << " " << filename;
    break;

  case ULS_ALREADY_LOADED:
    //LOG(ERR) << "Failed to already loaded " << type << " " << filename;
    break;

  case ULS_OK:
    break;
  }
}


bool rad1_load_standard_bits (int nth, bool force,
			      const std::string fpga_filename,
			      const std::string firmware_filename,
			      libusb_context *ctx)
{
  rad1LoadStatus    s;
  const char            *filename;
  const char            *proto_filename;
  int hw_rev;

  // start by loading the firmware
  s = rad1LoadFirmwareNth (nth, firmware_filename.data(), force, ctx);
  load_status_msg (s, "firmware", firmware_filename.data());

  if (s == ULS_ERROR)
    return false;

  // if we actually loaded firmware, we must reload fpga ...
  if (s == ULS_OK)
    force = true;

  libusb_device_handle *udh = openNthCmdInterface (nth, ctx);
  if (udh == 0)
    return false;

  unsigned char file_hash[RAD1_HASH_SIZE];
  unsigned char rad1_hash[RAD1_HASH_SIZE];

  filename = fpga_filename.data();

  if (access (filename, R_OK) != 0){
    perror (filename);
    s = ULS_ERROR;
  }
  else if (!computeHash (filename, file_hash))
    s =  ULS_ERROR;
  else if (!force
	   && rad1GetHash (udh, FPGA_HASH_SLOT, rad1_hash)
	   && memcmp (file_hash, rad1_hash, RAD1_HASH_SIZE) == 0) {
    s = ULS_ALREADY_LOADED;
    //printf("hash: %s %s\n",file_hash,rad1_hash);
  }
  else if (!rad1LoadFpga(udh, filename, file_hash))
    s = ULS_ERROR;
  else
    s = ULS_OK;
  
  libusb_close(udh);
  load_status_msg (s, "fpga bitstream", fpga_filename.data());
  
  if (s == ULS_ERROR)
    return false;
  
  return true;
}

rnrad1Core::rnrad1Core (int which_board,
			     int interface, int altInterface,
			     const std::string fpga_filename = "",
			     const std::string firmware_filename = "",
			     bool skipLoad = false)
{
  mudh = 0;
  mctx = 0;
  mUsbDataRate = 16000000;
  mBytesPerPoll = ((int) POLLING_INTERVAL*mUsbDataRate);
  mFpgaMasterClockFreq = 52000000;
  
  memset (mFpgaShadows, 0, sizeof (mFpgaShadows));

  if (libusb_init (&mctx) < 0)
    LOG(ERR) << "libusb_init failed";
  
  if (!skipLoad) {
    if (!rad1_load_standard_bits (which_board, false, fpga_filename,
                                  firmware_filename, mctx)) {
      LOG(ERR) << "File load failed";
      exit(1);
    }
  }
  
  libusb_device *dev = rad1FindDevice (which_board, false, mctx);
  if (dev == 0){
    LOG(ERR) << "Can't find RAD1 board " << which_board;
    exit(1);
  }

  libusb_device_descriptor desc;
  int ret = libusb_get_device_descriptor(dev, &desc);
  if (ret < 0) {
    LOG(ERR) << "usb_get_device_descriptor failed: " <<_get_usb_error_str(ret);
  }

  if ((desc.idVendor == USB_VID_FSF && desc.idProduct == USB_PID_FSF_RAD1) && ((desc.bcdDevice & 0xFF00) == 0)) {
    fprintf (stderr, "found unconfigured RAD1; needs firmware.\n");
  }

  if (desc.idVendor == USB_VID_CYPRESS && desc.idProduct == USB_PID_CYPRESS_FX2){
   fprintf (stderr, "found unconfigured FX2; needs firmware.\n");
  }

  mudh = rad1OpenInterface (dev, interface, altInterface);
  if (mudh == 0) {
    LOG(ERR) << "Can't open interfaces";
    exit(1);
  }

  // initialize registers that are common to rx and tx
  bool result=true;
  result &= write9862(REG_GENERAL,0);
  result &= write9862(REG_DLL,DLL_DISABLE_INTERNAL_XTAL_OSC | DLL_MULT_2X | DLL_FAST);
  result &= write9862(REG_CLKOUT,CLKOUT2_EQ_DLL_OVER_2);
  result &= write9862(REG_AUX_ADC_CLK,AUX_ADC_CLK_CLK_OVER_4);
  if (!result) {
    LOG(ERR) << "Failed to set common MSFE regs";
    exit(1);
  }

  writeFpgaReg (FR_MODE, 0);		// ensure we're in normal mode
  writeFpgaReg (FR_DEBUG_EN, 0);	// disable debug outputs
  }


rnrad1Core::~rnrad1Core ()
{
  if (mudh) libusb_close(mudh);
  if (mctx) libusb_exit(mctx);
}

void rnrad1Core::setUsbDataRate (int usb_data_rate) {
  mUsbDataRate = usb_data_rate;
  mBytesPerPoll = (int) (usb_data_rate*POLLING_INTERVAL);
}

int rnrad1Core::sendRqst (int request, bool flag)
{
  return usbMsg (mudh, request, flag, 0, 0, 0);
}

int rnrad1Core::checkUnderrun(unsigned char *status)
{
  return usbMsg (mudh, VRQ_GET_STATUS, 0, GS_TX_UNDERRUN,
		    status, 1);
}

int rnrad1Core::checkOverrun(unsigned char *status)
{
  return usbMsg (mudh, VRQ_GET_STATUS, 0, GS_RX_OVERRUN,
		    status, 1);
}

bool rnrad1Core::writeI2c (int i2c_addr, unsigned char *buf, int len)
{
  if (len < 1 || len > MAX_EP0_PKTSIZE)
    return false;
  
  return usbMsg (mudh, VRQ_I2C_WRITE, i2c_addr, 0,
		    buf, len) == len;
}

bool rnrad1Core::readI2c (int i2c_addr, unsigned char *buf, int len)
{
  if (len <= 0)
    return false; 
  
  if (usbMsg (mudh, VRQ_I2C_READ, i2c_addr, 0,
		 (unsigned char *) buf, len) != len)
    return false;
  
  return true;
}

bool rnrad1Core::setAdcOffset (int which_adc, int offset) 
{
  if (which_adc < 0 || which_adc > 3)
    return false;
  
  return writeFpgaReg (FR_ADC_OFFSET_0 + which_adc, offset);
}

bool rnrad1Core::setDacOffset (int which_dac, int offset, int offset_pin)
{
  int tx_a = (which_dac & 0x1) == 0;
  int lo = ((offset & 0x3) << 6) | (offset_pin & 0x1);
  int hi = (offset >> 2);
  bool ok;
  
  if (tx_a){
    ok =  write9862 (REG_TX_A_OFFSET_LO, lo);
    ok &= write9862 (REG_TX_A_OFFSET_HI, hi);
  }
  else {
    ok =  write9862 (REG_TX_B_OFFSET_LO, lo);
    ok &= write9862 (REG_TX_B_OFFSET_HI, hi);
  }
  
  return ok;
}

bool rnrad1Core::setAdcBufferBypass (int which_adc, bool bypass)
{
  if (which_adc < 0 || which_adc > 1)
    return false;
  
  bool useA = (which_adc & 1) == 0;
  int reg = useA ? REG_RX_A : REG_RX_B;
  
  unsigned char cur_rx;
  unsigned char cur_pwr_dn;
    
  // If the input buffer is bypassed, we need to power it down too.
  
  bool ok = read9862 (reg, &cur_rx);
  ok &= read9862 (REG_RX_PWR_DN, &cur_pwr_dn);
  if (!ok)
    return false;
  
  if (bypass){
    cur_rx |= RX_X_BYPASS_INPUT_BUFFER;
    cur_pwr_dn |= (useA ? RX_PWR_DN_BUF_A : RX_PWR_DN_BUF_B);
  }
  else {
    cur_rx &= ~RX_X_BYPASS_INPUT_BUFFER;
    cur_pwr_dn &= ~(useA ? RX_PWR_DN_BUF_A : RX_PWR_DN_BUF_B);
  }
    
  ok &= write9862 (reg, cur_rx);
  ok &= write9862 (REG_RX_PWR_DN, cur_pwr_dn);
  return ok;
}

bool rnrad1Core::setDcOffsetClEnable(int bits, int mask)
{
  return writeFpgaReg(FR_DC_OFFSET_CL_EN,
			 (mFpgaShadows[FR_DC_OFFSET_CL_EN] & ~mask) | (bits & mask));
}

bool rnrad1Core::writeAuxDac (int which_dac, int value) 
{
  if (!(0 <= which_dac && which_dac < 4)){
    LOG(ERR) << "Write to invalid dac" << which_dac;
    return false;
  }
    
  value &= 0x0fff;      // mask to 12-bits
  
  // dac 0, 1, and 2 are really 8 bits.
  value = value >> 4;         // shift value appropriately
  int regno = 36 + which_dac;
  
  return write9862(regno, value);
}

bool rnrad1Core::readAuxAdc (bool isTx, int which_adc, int *value)
{
  *value = 0;
  
  unsigned char aux_adc_control =
    AUX_ADC_CTRL_REFSEL_A               // on chip reference
    | AUX_ADC_CTRL_REFSEL_B;            // on chip reference
  
  int   rd_reg = 26;    // base address of two regs to read for result
  
  // program the ADC mux bits
  if (isTx)
    aux_adc_control |= AUX_ADC_CTRL_SELECT_A2 | AUX_ADC_CTRL_SELECT_B2;
  else {
    rd_reg += 2;
    aux_adc_control |= AUX_ADC_CTRL_SELECT_A1 | AUX_ADC_CTRL_SELECT_B1;
  }
  
  // I'm not sure if we can set the mux and issue a start conversion
  // in the same cycle, so let's do them one at a time.
  write9862(34, aux_adc_control);
  
  if (which_adc == 0)
    aux_adc_control |= AUX_ADC_CTRL_START_A;
  else {
    rd_reg += 4;
    aux_adc_control |= AUX_ADC_CTRL_START_B;
  }
  
  // start the conversion
  write9862(34, aux_adc_control);
  
  // read the 10-bit result back
  unsigned char v_lo = 0;
  unsigned char v_hi = 0;
  bool r = read9862(rd_reg, &v_lo);
  r &= read9862(rd_reg, &v_hi);
  
  if (r)
    *value = ((v_hi << 2) | ((v_lo >> 6) & 0x3)) << 2;  // format as 12-bit
  
  return r;
}

int rnrad1Core::readAuxAdc (bool isTx, int which_adc)
{
  int   value;
  if (!readAuxAdc (isTx, which_adc, &value))
    return READ_FAILED;
  
  return value;
}


bool rnrad1Core::writeEeprom (int i2c_addr, int eeprom_offset, const std::string buf)
{
  unsigned char cmd[2];
  const unsigned char *p = (unsigned char *) buf.data();
  
  // The simplest thing that could possibly work:
  //   all writes are single byte writes.
  //
  // We could speed this up using the page write feature,
  // but we write so infrequently, why bother...
  
  int len = buf.size();
  while (len-- > 0){
    cmd[0] = eeprom_offset++;
    cmd[1] = *p++;
    bool r = writeI2c (i2c_addr, cmd, 2);
    usleep (10*1000);                // delay 10ms worst case write time
    if (!r)
      return false;
  }
  
  return true;
}

std::string rnrad1Core::readEeprom (int i2c_addr, int eeprom_offset, int len)
{
  if (len <= 0)
    return "";
  
  char buf[len];
  
  unsigned char *p = (unsigned char *) buf;
  
  // We setup a random read by first doing a "zero byte write".
  // Writes carry an address.  Reads use an implicit address.
  
  unsigned char cmd[1];
  cmd[0] = eeprom_offset;
  if (!writeI2c(i2c_addr, cmd, 1)) return "";
  
  int orig_len = len;
  while (len > 0){
    int n = std::min (len, MAX_EP0_PKTSIZE);
    if (!readI2c(i2c_addr, p, n)) {return "";}
    len -= n;
    p += n;
  }
  
  return std::string (buf, orig_len);
}

bool rnrad1Core::setLed (int which_led, bool on)
{
  int r = usbMsg (mudh, VRQ_SET_LED, on, which_led, 0, 0);
  
  return r == 0;
}

bool rnrad1Core::writeFpgaReg (int regno, int value)	//< 7-bit regno, 32-bit value
{
  
  if (regno >= 0 && regno < MAX_REGS)
    mFpgaShadows[regno] = value;
  
  unsigned char buf[4];
  
  buf[0] = (value >> 24) & 0xff;        // MSB first  
  buf[1] = (value >> 16) & 0xff;
  buf[2] = (value >>  8) & 0xff;
  buf[3] = (value >>  0) & 0xff;
  
  return writeSpi (0x00 | (regno & 0x7f),
		     SPI_ENABLE_FPGA,
		     SPI_FMT_MSB | SPI_FMT_HDR_1,
		     buf, 4);
  
}

bool rnrad1Core::readFpgaReg (int regno, int *value)	//< 7-bit regno, 32-bit value
{
  *value = 0;
  unsigned char buf[4];
  
  bool ok = readSpi (0x80 | (regno & 0x7f),
		       SPI_ENABLE_FPGA,
		       SPI_FMT_MSB | SPI_FMT_HDR_1,
		       buf, 4);
  
  if (ok)
    *value = (buf[0] << 24) | (buf[1] << 16) | (buf[2] << 8) | buf[3];
  
  return ok;
}


int rnrad1Core::readFpgaReg (int regno)
{
  int value;
  if (!readFpgaReg (regno, &value))
    return READ_FAILED;
  return value;
}

bool rnrad1Core::write9862 (int regno, unsigned char value)
{
  unsigned char buf[1];
  
  buf[0] = value;
  
  return writeSpi(0x00 | (regno & 0x3f),
		    SPI_ENABLE_CODEC_A,
		    SPI_FMT_MSB | SPI_FMT_HDR_1,
		    (unsigned char *) buf, 1);
}

bool rnrad1Core::read9862 (int regno, unsigned char *value) const
{
  
  return readSpi(0x80 | (regno & 0x3f),
		   SPI_ENABLE_CODEC_A,
		   SPI_FMT_MSB | SPI_FMT_HDR_1,
		   value, 1);
}


int rnrad1Core::read9862 (int regno) const
{
  unsigned char value;
  if (!read9862 (regno, &value))
    return READ_FAILED;
  return value;
}


bool rnrad1Core::writeSpi (int optional_header, int enables, int format, unsigned char *buf, int len) 
{
  return (usbMsg (mudh, VRQ_SPI_WRITE,
		     optional_header,
		     ((enables & 0xff) << 8) | (format & 0xff),
		     buf, len) == len);
}


bool rnrad1Core::readSpi (int optional_header, int enables, int format, unsigned char *buf, int len) const
{
  if (len <= 0 || len > MAX_EP0_PKTSIZE)
    return false;
  
  if (usbMsg (mudh, VRQ_SPI_READ,
		 optional_header,
		 ((enables & 0xff) << 8) | (format & 0xff),
		 (unsigned char *) buf, len) != len)
    { return false;}
  
  return true;
}


bool rnrad1Core::start() 
{
  return true;
}

