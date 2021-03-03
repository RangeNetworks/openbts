/* -*- c++ -*- */
/*
 * USRP - Universal Software Radio Peripheral
 *
 * Copyright (C) 2003, 2004, 2009 Free Software Foundation, Inc.
 * Copyright 2014 Range Networks, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Boston, MA  02110-1301  USA
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <assert.h>
#include <errno.h>

#include <Logger.h>
#include <Configuration.h>

ConfigurationTable gConfig;

using namespace std;

#include "rnrad1Core.h"

char *prog_name;

static void
set_progname (char *path)
{
  char *p = strrchr (path, '/');
  if (p != 0)
    prog_name = p+1;
  else
    prog_name = path;
}

static void
usage ()
{
  fprintf (stderr, "usage: \n");
  fprintf (stderr, "  %s [-v] [-w <which_board>] [-x] ...\n", prog_name);
  fprintf (stderr, "  %s load_standard_bits\n", prog_name);
  fprintf (stderr, "  %s load_firmware <file.ihx>\n", prog_name);
  fprintf (stderr, "  %s load_fpga <file.rbf>\n", prog_name);
  fprintf (stderr, "  %s write_fpga_reg <reg8> <value32>\n", prog_name);
  fprintf (stderr, "  %s set_fpga_reset {on|off}\n", prog_name);
  fprintf (stderr, "  %s set_fpga_tx_enable {on|off}\n", prog_name);
  fprintf (stderr, "  %s set_fpga_rx_enable {on|off}\n", prog_name);
  fprintf (stderr, "  ----- diagnostic routines -----\n");
  fprintf (stderr, "  %s led0 {on|off}\n", prog_name);
  fprintf (stderr, "  %s led1 {on|off}\n", prog_name);
  fprintf (stderr, "  %s set_hash0 <hex-string>\n", prog_name);
  fprintf (stderr, "  %s get_hash0\n", prog_name);
  fprintf (stderr, "  %s i2c_read i2c_addr len\n", prog_name);
  fprintf (stderr, "  %s i2c_write i2c_addr <hex-string>\n", prog_name);
  fprintf (stderr, "  %s 9862a_write regno value\n", prog_name);
  fprintf (stderr, "  %s 9862a_read regno\n", prog_name);
  exit (1);
}

#if 0
static void
die (const char *msg)
{
  fprintf (stderr, "%s (die): %s\n", prog_name, msg);
  exit (1);
}
#endif

static int 
hexval (char ch)
{
  if ('0' <= ch && ch <= '9')
    return ch - '0';

  if ('a' <= ch && ch <= 'f')
    return ch - 'a' + 10;

  if ('A' <= ch && ch <= 'F')
    return ch - 'A' + 10;

  return -1;
}

static unsigned char *
hex_string_to_binary (const char *string, int *lenptr)
{
  int	sl = strlen (string);
  if (sl & 0x01){
    fprintf (stderr, "%s: odd number of chars in <hex-string>\n", prog_name);
    return 0;
  }

  int len = sl / 2;
  *lenptr = len;
  unsigned char *buf = new unsigned char [len];

  for (int i = 0; i < len; i++){
    int hi = hexval (string[2 * i]);
    int lo = hexval (string[2 * i + 1]);
    if (hi < 0 || lo < 0){
      fprintf (stderr, "%s: invalid char in <hex-string>\n", prog_name);
      delete [] buf;
      return 0;
    }
    buf[i] = (hi << 4) | lo;
  }
  return buf;
}

static void
print_hex (FILE *fp, unsigned char *buf, int len)
{
  for (int i = 0; i < len; i++){
    fprintf (fp, "%02x", buf[i]);
  }
  fprintf (fp, "\n");
}

static void
chk_result (bool ok)
{
  if (!ok){
    fprintf (stderr, "%s: failed\n", prog_name);
    exit (1);
  }
}

static bool
get_on_off (const char *s)
{
  if (strcmp (s, "on") == 0)
    return true;

  if (strcmp (s, "off") == 0)
    return false;

  usage ();			// no return
  return false;
}


int
main (int argc, char **argv)
{
  int		ch;
  bool		verbose = false;
  int		which_board = 0;
  bool		fx2_ok_p = false;
  
  set_progname (argv[0]);
  
  while ((ch = getopt (argc, argv, "vw:x")) != EOF){
    switch (ch){

    case 'v':
      verbose = true;
      break;
      
    case 'w':
      which_board = strtol (optarg, 0, 0);
      break;
      
    case 'x':
      fx2_ok_p = true;
      break;
      
    default:
      usage ();
    }
  }

  int nopts = argc - optind;

  if (nopts < 1)
    usage ();

  const char *cmd = argv[optind++];
  nopts--;

  gLogInit("openbts",NULL,LOG_LOCAL7);

  rnrad1Core *core = new rnrad1Core(which_board,
				    RAD1_CMD_INTERFACE,
				    RAD1_CMD_ALTINTERFACE,
				    "","",true);

#define CHKARGS(n) if (nopts != n) usage (); else

  if (strcmp (cmd, "led0") == 0){
    CHKARGS (1);
    bool on = get_on_off (argv[optind]);
    chk_result (core->setLed(0, on));
  }
  else if (strcmp (cmd, "led1") == 0){
    CHKARGS (1);
    bool on = get_on_off (argv[optind]);
    chk_result (core->setLed(1, on));
  }
  else if (strcmp (cmd, "led2") == 0){
    CHKARGS (1);
    bool on = get_on_off (argv[optind]);
    chk_result (core->setLed (2, on));
  }
  else if (strcmp (cmd, "set_hash0") == 0){
    CHKARGS (1);
    char *p = argv[optind];
    unsigned char buf[16];

    memset (buf, ' ', 16);
    for (int i = 0; i < 16 && *p; i++)
      buf[i] = *p++;
    
    chk_result (rad1SetHash (core->getHandle(), 0, buf));
  }
  else if (strcmp (cmd, "get_hash0") == 0){
    CHKARGS (0);
    unsigned char buf[17];
    memset (buf, 0, 17);
    bool r = rad1GetHash (core->getHandle(), 0, buf);
    if (r)
      printf ("hash: %s\n", buf);
    chk_result (r);
  }
  else if (strcmp (cmd, "load_fpga") == 0){
    CHKARGS (1);
    char *filename = argv[optind];
    unsigned char hash[RAD1_HASH_SIZE];
    chk_result (rad1LoadFpga (core->getHandle(), filename, hash));
  }
  else if (strcmp (cmd, "load_firmware") == 0){
    CHKARGS (1);
    char *filename = argv[optind];
    unsigned char hash[RAD1_HASH_SIZE];
    chk_result (rad1LoadFirmware (core->getHandle(), filename, hash));
  }
  else if (strcmp (cmd, "write_fpga_reg") == 0){
    CHKARGS (2);
    chk_result (core->writeFpgaReg (strtoul (argv[optind], 0, 0),
				    strtoul(argv[optind+1], 0, 0)));
  }
  else if (strcmp (cmd, "set_fpga_reset") == 0){
    CHKARGS (1);
    chk_result (usbMsg(core->getHandle(), VRQ_FPGA_SET_RESET, get_on_off (argv[optind]), 0, 0, 0));
  }
  else if (strcmp (cmd, "set_fpga_tx_enable") == 0){
    CHKARGS (1);
    chk_result (usbMsg(core->getHandle(), VRQ_FPGA_SET_TX_ENABLE, get_on_off (argv[optind]), 0, 0, 0));
  }
  else if (strcmp (cmd, "set_fpga_rx_enable") == 0){
    CHKARGS (1);
    chk_result (usbMsg(core->getHandle(), VRQ_FPGA_SET_RX_ENABLE, get_on_off (argv[optind]), 0, 0, 0));
  }
  else if (strcmp (cmd, "load_standard_bits") == 0){
    CHKARGS (0);
    libusb_close(core->getHandle());
    chk_result (rad1_load_standard_bits (which_board, true,"ezusb.ihx","fpga.rbf",core->getContext()));
  }
  else if (strcmp (cmd, "i2c_read") == 0){
    CHKARGS (2);
    int	i2c_addr = strtol (argv[optind], 0, 0);
    int len = strtol (argv[optind + 1], 0, 0);
    if (len < 0)
      chk_result (0);

    unsigned char *buf = new unsigned char [len];
    bool result = core->readI2c(i2c_addr, buf, len);
    if (!result){
      chk_result (0);
    }
    print_hex (stdout, buf, len);
  }
  else if (strcmp (cmd, "i2c_write") == 0){
    CHKARGS (2);
    int	i2c_addr = strtol (argv[optind], 0, 0);
    int	len = 0;
    char *hex_string  = argv[optind + 1];
    unsigned char *buf = hex_string_to_binary (hex_string, &len);
    if (buf == 0)
      chk_result (0);

    bool result = core->writeI2c(i2c_addr, buf, len);
    chk_result (result);
  }
  else if (strcmp (cmd, "9862a_write") == 0){
    CHKARGS (2);
    int regno = strtol (argv[optind], 0, 0);
    int value = strtol (argv[optind+1], 0, 0);
    chk_result (core->write9862(regno, value));
  }
  else if (strcmp (cmd, "9862a_read") == 0){
    CHKARGS (1);
    int regno = strtol (argv[optind], 0, 0);
    unsigned char value;
    bool result = core->read9862(regno, &value);
    if (!result){
      chk_result (0);
    }
    fprintf (stdout, "reg[%d] = 0x%02x\n", regno, value);
  }
  else {
    usage ();
  }

  return 0;

  delete core;
}
