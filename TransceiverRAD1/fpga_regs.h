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

#ifndef INCLUDED_FPGA_REGS_H
#define INCLUDED_FPGA_REGS_H

// This file defines registers common to all FPGA configurations.
// Registers 0 to 31 are reserved for use in this file.


// The FPGA needs to know the rate that samples are coming from and
// going to the A/D's and D/A's.  div = 128e6 / sample_rate

#define	FR_TX_SAMPLE_RATE_DIV	 0
#define	FR_RX_SAMPLE_RATE_DIV	 1

// 2 and 3 are defined in the ATR section

#define	FR_MASTER_CTRL		 4	// master enable and reset controls
#  define  bmFR_MC_ENABLE_TX		(1 << 0)
#  define  bmFR_MC_ENABLE_RX		(1 << 1)
#  define  bmFR_MC_RESET_TX		(1 << 2)
#  define  bmFR_MC_RESET_RX		(1 << 3)

// i/o direction registers for pins that go to daughterboards.
// Setting the bit makes it an output from the FPGA to the d'board.
// top 16 is mask, low 16 is value

#define	FR_OE_0			 5	// slot 0
#define	FR_OE_1			 6
#define	FR_OE_2			 7
#define	FR_OE_3			 8

// i/o registers for pins that go to daughterboards.
// top 16 is a mask, low 16 is value

#define	FR_IO_0			 9	// slot 0
#define	FR_IO_1			10
#define	FR_IO_2			11
#define	FR_IO_3			12

#define	FR_MODE			13
#  define  bmFR_MODE_NORMAL		      0
#  define  bmFR_MODE_LOOPBACK		(1 << 0)	// enable digital loopback
#  define  bmFR_MODE_RX_COUNTING	(1 << 1)	// Rx is counting
#  define  bmFR_MODE_RX_COUNTING_32BIT	(1 << 2)	// Rx is counting with a 32 bit counter
                                                    // low and high 16 bits are multiplexed across channel I and Q


// If the corresponding bit is set, internal FPGA debug circuitry
// controls the i/o pins for the associated bank of daughterboard
// i/o pins.  Typically used for debugging FPGA designs.

#define FR_DEBUG_EN             14
#  define bmFR_DEBUG_EN_TX_A           (1 << 0)        // debug controls TX_A i/o
#  define bmFR_DEBUG_EN_RX_A           (1 << 1)        // debug controls RX_A i/o
#  define bmFR_DEBUG_EN_TX_B           (1 << 2)        // debug controls TX_B i/o
#  define bmFR_DEBUG_EN_RX_B           (1 << 3)        // debug controls RX_B i/o


// If the corresponding bit is set, enable the automatic DC
// offset correction control loop.
//
// The 4 low bits are significant:
//
//   ADC0 = (1 << 0)
//   ADC1 = (1 << 1)
//   ADC2 = (1 << 2)
//   ADC3 = (1 << 3)
//
// This control loop works if the attached daugherboard blocks DC.
// Currently all daughterboards do block DC.  This includes:
// basic rx, dbs_rx, tv_rx, flex_xxx_rx.

#define FR_DC_OFFSET_CL_EN	15			// DC Offset Control Loop Enable


// offset corrections for ADC's and DAC's (2's complement)

#define	FR_ADC_OFFSET_0	 	16
#define	FR_ADC_OFFSET_1		17
#define	FR_ADC_OFFSET_2	 	18
#define	FR_ADC_OFFSET_3		19


// ------------------------------------------------------------------------
// Automatic Transmit/Receive switching
//
// If automatic transmit/receive (ATR) switching is enabled in the
// FR_ATR_CTL register, the presence or absence of data in the FPGA
// transmit fifo selects between two sets of values for each of the 4
// banks of daughterboard i/o pins.
//
// Each daughterboard slot has 3 16-bit registers associated with it:
//   FR_ATR_MASK_*, FR_ATR_TXVAL_* and FR_ATR_RXVAL_*
//
// FR_ATR_MASK_{0,1,2,3}: 
//
//   These registers determine which of the daugherboard i/o pins are
//   affected by ATR switching.  If a bit in the mask is set, the
//   corresponding i/o bit is controlled by ATR, else it's output
//   value comes from the normal i/o pin output register:
//   FR_IO_{0,1,2,3}.
//
// FR_ATR_TXVAL_{0,1,2,3}:
// FR_ATR_RXVAL_{0,1,2,3}:
//
//   If the Tx fifo contains data, then the bits from TXVAL that are
//   selected by MASK are output.  Otherwise, the bits from RXVAL that
//   are selected by MASK are output.
                      
#define FR_ATR_MASK_0		20	// slot 0
#define	FR_ATR_TXVAL_0		21
#define FR_ATR_RXVAL_0		22

#define FR_ATR_MASK_1		23	// slot 1
#define	FR_ATR_TXVAL_1		24
#define FR_ATR_RXVAL_1		25

#define FR_ATR_MASK_2		26	// slot 2
#define	FR_ATR_TXVAL_2		27
#define FR_ATR_RXVAL_2		28

#define FR_ATR_MASK_3		29	// slot 3
#define	FR_ATR_TXVAL_3		30
#define FR_ATR_RXVAL_3		31

// Clock ticks to delay rising and falling edge of T/R signal
#define FR_ATR_TX_DELAY          2
#define FR_ATR_RX_DELAY          3

// Register numbers 0 to 31 are reserved for use in fpga_regs_common.h.
// Registers 64 to 79 are available for custom FPGA builds.


// DDC / DUC

#define	FR_INTERP_RATE		32	// [1,1024]
#define	FR_DECIM_RATE		33	// [1,256]

// DDC center freq

#define FR_RX_FREQ_0		34
#define FR_RX_FREQ_1		35
#define FR_RX_FREQ_2		36
#define FR_RX_FREQ_3		37

// See below for DDC Starting Phase

// ------------------------------------------------------------------------
//  configure FPGA Rx mux
//
//    3                   2                   1                       
//  1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0
// +-----------------------+-------+-------+-------+-------+-+-----+
// |      must be zero     | Q3| I3| Q2| I2| Q1| I1| Q0| I0|Z| NCH |
// +-----------------------+-------+-------+-------+-------+-+-----+
//
// There are a maximum of 4 digital downconverters in the the FPGA.
// Each DDC has two 16-bit inputs, I and Q, and two 16-bit outputs, I & Q.
//
// DDC I inputs are specified by the two bit fields I3, I2, I1 & I0
//
//   0 = DDC input is from ADC 0
//   1 = DDC input is from ADC 1
//   2 = DDC input is from ADC 2
//   3 = DDC input is from ADC 3
//
// If Z == 1, all DDC Q inputs are set to zero
// If Z == 0, DDC Q inputs are specified by the two bit fields Q3, Q2, Q1 & Q0
//
// NCH specifies the number of complex channels that are sent across
// the USB.  The legal values are 1, 2 or 4, corresponding to 2, 4 or
// 8 16-bit values.

#define	FR_RX_MUX		38

// ------------------------------------------------------------------------
//  configure FPGA Tx Mux.
//
//    3                   2                   1                       
//  1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0
// +-----------------------+-------+-------+-------+-------+-+-----+
// |                       | DAC3  | DAC2  | DAC1  |  DAC0 |0| NCH |
// +-----------------------------------------------+-------+-+-----+
//
// NCH specifies the number of complex channels that are sent across
// the USB.  The legal values are 1 or 2, corresponding to 2 or 4
// 16-bit values.
//
// There are two interpolators with complex inputs and outputs.
// There are four DACs.  (We use the DUC in each AD9862.)
//
// Each 4-bit DACx field specifies the source for the DAC and
// whether or not that DAC is enabled.  Each subfield is coded
// like this: 
//
//    3 2 1 0
//   +-+-----+
//   |E|  N  |
//   +-+-----+
//
// Where E is set if the DAC is enabled, and N specifies which
// interpolator output is connected to this DAC.
//
//  N   which interp output
// ---  -------------------
//  0   chan 0 I
//  1   chan 0 Q
//  2   chan 1 I
//  3   chan 1 Q

#define	FR_TX_MUX		39

// ------------------------------------------------------------------------
// REFCLK control
//
// Control whether a reference clock is sent to the daughterboards,
// and what frequency.  The refclk is sent on d'board i/o pin 0.
//
//    3                   2                   1                       
//  1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0
// +-----------------------------------------------+-+------------+
// |             Reserved (Must be zero)           |E|   DIVISOR  |
// +-----------------------------------------------+-+------------+

//
// Bit 7  -- 1 turns on refclk, 0 allows IO use
// Bits 6:0 Divider value

#define FR_TX_A_REFCLK          40
#define FR_RX_A_REFCLK          41
#define FR_TX_B_REFCLK          42
#define FR_RX_B_REFCLK          43

#  define bmFR_REFCLK_EN	   0x80
#  define bmFR_REFCLK_DIVISOR_MASK 0x7f

// ------------------------------------------------------------------------
// DDC Starting Phase

#define FR_RX_PHASE_0		44
#define FR_RX_PHASE_1		45
#define FR_RX_PHASE_2		46
#define FR_RX_PHASE_3		47

// ------------------------------------------------------------------------
// Tx data format control register
//
//    3                   2                   1                       
//  1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0
// +-------------------------------------------------------+-------+
// |                    Reserved (Must be zero)            |  FMT  |
// +-------------------------------------------------------+-------+
//
//  FMT values:

#define FR_TX_FORMAT		48
#  define bmFR_TX_FORMAT_16_IQ		0	// 16-bit I, 16-bit Q

// ------------------------------------------------------------------------
// Rx data format control register
//
//    3                   2                   1                       
//  1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0
// +-----------------------------------------+-+-+---------+-------+
// |          Reserved (Must be zero)        |B|Q|  WIDTH  | SHIFT |
// +-----------------------------------------+-+-+---------+-------+
//
//  FMT values:

#define FR_RX_FORMAT		49

#  define bmFR_RX_FORMAT_SHIFT_MASK	(0x0f <<  0)	// arithmetic right shift [0, 15]
#  define bmFR_RX_FORMAT_SHIFT_SHIFT	0
#  define bmFR_RX_FORMAT_WIDTH_MASK	(0x1f <<  4)    // data width in bits [1, 16] (not all valid)
#  define bmFR_RX_FORMAT_WIDTH_SHIFT    4
#  define bmFR_RX_FORMAT_WANT_Q		(0x1  <<  9)    // deliver both I & Q, else just I
#  define bmFR_RX_FORMAT_BYPASS_HB	(0x1  << 10)    // bypass half-band filter

// The valid combinations currently are:
//
//   B  Q  WIDTH  SHIFT
//   0  1    16     0
//   0  1     8     8


// Possible future values of WIDTH = {4, 2, 1}
// 12 takes a bit more work, since we need to know packet alignment.

// ------------------------------------------------------------------------
// FIXME register numbers 50 to 63 are available

// ------------------------------------------------------------------------
// Registers 64 to 95 are reserved for user custom FPGA builds.
// The standard RAD1 software will not touch these.

#define FR_USER_0	64
#define FR_USER_1	65
#define FR_USER_2	66
#define FR_USER_3	67
#define FR_USER_4	68
#define FR_USER_5	69
#define FR_USER_6	70
#define FR_USER_7	71
#define FR_USER_8	72
#define FR_USER_9	73
#define FR_USER_10	74
#define FR_USER_11	75
#define FR_USER_12	76
#define FR_USER_13	77
#define FR_USER_14	78
#define FR_USER_15	79
#define FR_USER_16	80
#define FR_USER_17	81
#define FR_USER_18	82
#define FR_USER_19	83
#define FR_USER_20	84
#define FR_USER_21	85
#define FR_USER_22	86
#define FR_USER_23	87
#define FR_USER_24	88
#define FR_USER_25	89
#define FR_USER_26	90
#define FR_USER_27	91
#define FR_USER_28	92
#define FR_USER_29	93
#define FR_USER_30	94
#define FR_USER_31	95

//Registers needed for multi rad1 master/slave configuration
//
//Rx Master/slave control register (FR_RX_MASTER_SLAVE = FR_USER_0)
//
#define FR_RX_MASTER_SLAVE		64
#define bitnoFR_RX_SYNC			0
#define bitnoFR_RX_SYNC_MASTER		1
#define bitnoFR_RX_SYNC_SLAVE		2
#  define bmFR_RX_SYNC          (1 <<bitnoFR_RX_SYNC) //1 If this is a master "sync now" and send sync to slave.
                                                      //  If this is a slave "sync now" (testing purpose only)
                                                      //  Sync is allmost the same as reset (clear all counters and buffers) 
                                                      //  except that the io outputs and settings don't get reset (otherwise it couldn't send the sync to the slave)
                                                      //0 Normal operation

#  define bmFR_RX_SYNC_MASTER (1 <<bitnoFR_RX_SYNC_MASTER) //1 This is a rx sync master, output sync_rx on rx_a_io[15]
                                                           //0 This is not a rx sync master
#  define bmFR_RX_SYNC_SLAVE  (1 <<bitnoFR_RX_SYNC_SLAVE) //1 This is a rx sync slave, follow sync_rx on rx_a_io[bitnoFR_RX_SYNC_INPUT_IOPIN]
                                                          //0 This is not an rx sync slave.

//Caution The master settings will output values on the io lines.
//They inheritely enable these lines as output. If you have a daughtercard which uses these lines also as output then you will burn your rad1 and daughtercard.
//If you set the slave bits then your rad1 won't do anything if you don't connect a master.
// Rx Master/slave control register
//
// The way this is supposed to be used is connecting a (short) 16pin flatcable from an rx daughterboard in RXA master io_rx[8..15] to slave io_rx[8..15] on RXA of slave rad1
// This can be done with basic_rx boards or dbsrx boards
//dbsrx: connect master-J25 to slave-J25
//basic rx: connect J25 to slave-J25
//CAUTION: pay attention to the lineup of your connector.
//The red line (pin1) should be at the same side of the daughterboards on master and slave.
//If you turnaround the cable on one end you will burn your rad1.

//You cannot use a 16pin flatcable if you are using FLEX400 or FLEX2400 daughterboards, since these use a lot of the io pins.
//You can still link them but you must use only a 2pin or 1pin cable
//You can also use a 2-wire link. put a 2pin header on io[15],gnd of the master RXA daughterboard and connect it to io15,gnd of the slave RXA db.
//You can use a cable like the ones found with the leds on the mainbord of a PC.
//Make sure you don't twist the cable, otherwise you connect the sync output to ground.
//To be save you could also just use a single wire from master io[15] to slave io[15], but this is not optimal for signal integrity.


// Since rx_io[0] can normally be used as a refclk and is not exported on all daughterboards this line
// still has the refclk function if you use the master/slave setup (it is not touched by the master/slave settings).
// The master/slave circuitry will only use io pin 15 and does not touch any of the other io pins.
#define bitnoFR_RX_SYNC_INPUT_IOPIN		15
#define bmFR_RX_SYNC_INPUT_IOPIN		(1<<bitnoFR_RX_SYNC_INPUT_IOPIN)
//TODO the output pin is still hardcoded in the verilog code, make it listen to the following define
#define bitnoFR_RX_SYNC_OUTPUT_IOPIN	15
#define bmFR_RX_SYNC_OUTPUT_IOPIN	(1<<bitnoFR_RX_SYNC_OUTPUT_IOPIN)
// =======================================================================
// READBACK Registers
// =======================================================================

#define FR_RB_IO_RX_A_IO_TX_A	1	// read back a-side i/o pins
#define	FR_RB_IO_RX_B_IO_TX_B	2	// read back b-side i/o pins

// ------------------------------------------------------------------------
// FPGA Capability register
//
//    3                   2                   1                       
//  1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0
// +-----------------------------------------------+-+-----+-+-----+
// |                    Reserved (Must be zero)    |T|NDUC |R|NDDC |
// +-----------------------------------------------+-+-----+-+-----+
//
// Bottom 4-bits are Rx capabilities
// Next   4-bits are Tx capabilities

#define	FR_RB_CAPS	3
#  define bmFR_RB_CAPS_NDDC_MASK	(0x7 << 0)   // # of digital down converters 0,1,2,4
#  define bmFR_RB_CAPS_NDDC_SHIFT	0
#  define bmFR_RB_CAPS_RX_HAS_HALFBAND	(0x1 << 3)
#  define bmFR_RB_CAPS_NDUC_MASK        (0x7 << 4)   // # of digital up converters 0,1,2
#  define bmFR_RB_CAPS_NDUC_SHIFT	4
#  define bmFR_RB_CAPS_TX_HAS_HALFBAND	(0x1 << 7)


#endif /* INCLUDED_FPGA_REGS_H */
