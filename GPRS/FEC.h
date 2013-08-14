/*
* Copyright 2011 Range Networks, Inc.
* All Rights Reserved.
*
* This software is distributed under multiple licenses;
* see the COPYING file in the main directory for licensing
* information for this specific distribuion.
*
* This use of this software may be subject to additional restrictions.
* See the LEGAL file in the main directory for details.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
*/

/**@file GPRS L1 radio channels, from GSM 05.02 and 05.03. */

#ifndef GPRSL1FEC_H
#define GPRSL1FEC_H

#include <GSMCommon.h>
#include <GSMConfig.h>		// for gBTS
#include <GSML1FEC.h>
#include <GSMTransfer.h>	// for TxBurst
#include <GSMLogicalChannel.h> // for TCHFACCHLogicalChannel
#include "MAC.h"
using namespace GSM;
namespace GPRS {
class TBF;

// GSM05.03 sec 5.1.4 re GPRS CS-4 says: 16 bit parity with generator: D16 + D12 + D5 + 1,
static const unsigned long sCS4Generator = (1<<16) + (1<<12) + (1<<5) + 1;

class PDCHL1FEC;
class PDCHCommon
{
	public:
	PDCHL1FEC *mchParent;

	PDCHCommon(PDCHL1FEC*wParent) { mchParent = wParent; }
	ARFCNManager *getRadio();
	unsigned TN();
	unsigned ARFCN();
	unsigned CN();
	L1Decoder *getDecoder() const;
	float FER() const;
	void countGoodFrame();
	void countBadFrame();
	PDCHL1Uplink *uplink();
	PDCHL1Downlink *downlink();
	PDCHL1FEC *parent();	// If called from mchParent, returns itself.
	TBF *getTFITBF(int tfi, RLCDirType dir);
	void setTFITBF(int tfi, RLCDirType dir, TBF *TBF);
	
	const char *getAnsweringUsfText(char *buf, RLCBSN_t bsn); // Printable USFs around BSN for debugging

	char *shortId() {	// Return a short printable id for this channel.
		static char buf[20];
		sprintf(buf,"PDCH#%u:%u",getRadio()->ARFCN(),TN());
		return buf;
	}
};

// For gprs channels, should we allocate them using getTCH(),
// which returns a TCHFACCHLogicalChannel which we have no use for,
// or should we get dedicated GPRS channels directly from TRXManager,
// which currently does not allow this.
// Answer: We are going to make the logical channels tri-state (inactive, RRactive, GPRSactive),
// and use getTCH to get them.

// There is one of these classes for each GPRS Data Channel, PDTCH.
// Downstream it attaches to a single Physical channel in L1FEC via mchEncoder and mchDecoder.
// TODO: I did this wrong.  This should be for a single ARFCN, but multiple
// upstream/downstream timeslots.
class PDCHL1FEC :
	public L1UplinkReservation,
	public PDCHCommon,
	public USFList
{
	public:
	PDCHL1Downlink *mchDownlink;
	PDCHL1Uplink *mchUplink;

	L1FEC *mchOldFec;		// The GSM TCH channel that this GPRS channel took over;
						// it has the channel parameters.

	// Temporary: GPRS will not use anything in this LogicalChannel class, and we dont want
	// the extra class hanging around, but currently the only way to dynamically
	// allocate physical channels is via the associated logical channel.
	TCHFACCHLogicalChannel *mchLogChan;

	public:
	// The TFIs are a 5 bit handle for TBFs.  The USFs are a 3 bit handle for uplink TBFs.
	TFIList *mchTFIs;// Points to the global TFIList.  Someday will point to the per-ARFCN TFIList.

	void debug_test();

	PDCHL1FEC(TCHFACCHLogicalChannel *wlogchan);

	// Release the radio channel.
	// GSM will start using it immediately.  TODO: Do we want to set a timer
	// so it is not reused immediately?
	~PDCHL1FEC();

	// Attach this GPRS channel to the specified GSM channel.
	//void mchOpen(TCHFACCHLogicalChannel *wlogchan);

	void mchStart();
	void mchStop();
	void mchDump(std::ostream&os,bool verbose);

	// Return a description of PDTCH, which is the only one we care about.
	// (We dont care about the associated SDCCH, whose frame is used in GPRS for
	// continuous timing advance.)
	// The packet channel description is the same as channel description except:
	// From GSM04.08 10.5.25 table 10.5.2.25a table 10.5.58, and I quote:
	// "The Channel type field (5 bit) shall be ignored by the receiver and
	// all bits treated as spare. For backward compatibility
	// reasons, the sender shall set the spare bits to binary '00001'."
	// This doesnt matter in the slightest, because the typeAndOffset would
	// have been TCHF_0 whose enum value is 1 anyway.
	L3ChannelDescription packetChannelDescription()
	{
		L1FEC *lf = mchOldFec;
		return L3ChannelDescription((TypeAndOffset) 1, lf->TN(), lf->TSC(), lf->ARFCN());
	}
};
std::ostream& operator<<(std::ostream& os, PDCHL1FEC *ch);

// For CS-1 decoding, just uses SharedL1Decoder.
// For CS-4 decoding: Uses the SharedL1Decoder through deinterleaving into mC.
class GprsDecoder : public SharedL1Decoder
{
	Parity mBlockCoder_CS4;
	BitVector mDP_CS4;
	public:
	BitVector mD_CS4;
	short qbits[8];
	ChannelCodingType getCS();	// Determine CS from the qbits.
	BitVector *getResult();
	GprsDecoder() :
		mBlockCoder_CS4(sCS4Generator,16,431+16),
		mDP_CS4(431+16),
		mD_CS4(mDP_CS4.head(424))
		{}
	bool decodeCS4();
};

// CS-4 has 431 input data bits, which are always 424 real data bits (53 bytes)
// plus 7 unused bits that are set to 0, to make 431 data bits.
// The first 3 bits are usf encoded to 12 bits, to yield 440 bits.
// Then 16 bit parity bits yields 456 bits.
class GprsEncoder : public SharedL1Encoder
{
	Parity mBlockCoder_CS4;
	public:
	// Uses SharedL1Encoder::mC for result vector
	// Uses SharedL1Encoder::mI for the 4-way interleaved result vector.
	BitVector mP_CS4;	// alias for parity part of mC
	BitVector mU_CS4;	// alias for usf part of mC
	BitVector mD_CS4;	// assembly area for parity.
	GprsEncoder() :
		SharedL1Encoder(),
		mBlockCoder_CS4(sCS4Generator,16,431+16),
		mP_CS4(mC.segment(440,16)),
		mU_CS4(mC.segment(0,12)),
		mD_CS4(mC.segment(12-3,431))
		{}
	void encodeCS4(const BitVector&src);
	void encodeCS1(const BitVector &src);
};



class PDCHL1Uplink : public PDCHCommon
{
	protected:
#if GPRS_ENCODER
	//SharedL1Decoder mchCS1Dec;
	GprsDecoder mchCS14Dec;
#else	// This case does not compile yet.
	L1Decoder mchCS1Dec;
#endif

	public:
	static const RLCDirType mchDir = RLCDir::Up;
	// The uplink queue:
	// There will typically only be one guy on here, and we could probably dispense
	// with the queue, but it is safer to do it this way to avoid thread problems.
	// InterthreadQueue template adds "*" so it is really a queue of BitVector*
	InterthreadQueue<RLCRawBlock> mchUplinkData;

	PDCHL1Uplink(PDCHL1FEC *wParent) : PDCHCommon(wParent) { }

	~PDCHL1Uplink() {}

	void writeLowSideRx(const RxBurst &inBurst);

	// TODO: This needs to be per-MS.
	// void setPhy(float wRSSI, float TimingError) {
		// This function is inapplicable to packet channels, which have multiple
		// MS listening to the same channel.
		//assert(0);
	//}
};


// One of these for each PDCH (physical channel), attached to L1FEC.
// Accepts Radio Blocks from anybody.
// Based on SACCHL1Encoder and TCHFACCHL1Encoder
// This does on-demand sending of RLCBlocks down to the physical channel.
// We wait until the last minute so we can encode uplink assignments in the blocks
// as close as possible to the present time.
// TODO: When we support different encodings we may have to base this on L1Encoder directly
// and copy a bunch of routines from XCCHL1Encoder?
static const int qCS1[8] = { 1,1,1,1,1,1,1,1 };
static const int qCS2[8] = { 1,1,0,0,1,0,0,0 }; // GSM05.03 sec 5.1.2.5
static const int qCS3[8] = { 0,0,1,0,0,0,0,1 }; // GSM05.03 sec 5.1.3.5
static const int qCS4[8] = { 0,0,0,1,0,1,1,0 }; // GSM0503 sec5.1.4.5; magically identifies CS-4.

class PDCHL1Downlink : public PDCHCommon
{
	protected:
#if GPRS_ENCODER
	GprsEncoder mchEnc;
	//GSM::SharedL1Encoder mchCS1Enc;
	//GSM::SharedL1Encoder mchCS4Enc;
#else
	GSM::L1Encoder mchCS1Enc;
#endif
	TxBurst mchBurst;					///< a preformatted burst template
	//TxBurst mchFillerBurst;			// unused ///< the filler burst for this channel
	int mchTotalBursts;
	//GSM::Time mchNextWriteTime, mchPrevWriteTime;
	const TDMAMapping& mchMapping;
	BitVector mchIdleFrame;

	// The mDownlinkData is used only for control messages, which can stack up.
	//InterthreadQueue<RLCDownlinkMessage> mchDownlinkMsgQ;

	public:
	static const RLCDirType mchDir = RLCDir::Up;

	void initBursts(L1FEC*);
	PDCHL1Downlink(PDCHL1FEC *wParent) :
		PDCHCommon(wParent),
		//mchCS1Enc(ChannelCodingCS1),
#if GPRS_ENCODER
		//mchCS4Enc(ChannelCodingCS4),
#endif
		mchTotalBursts(0),
		mchMapping(wParent->mchOldFec->encoder()->mapping()),
		mchIdleFrame((size_t)0)
	{
	 	initBursts(wParent->mchOldFec);
	}

	~PDCHL1Downlink() {}

	// Enqueue a downlink message.  We dont use this for downlink data - those
	// are sent by calling the RLCEngine when this queue is empty.
	//void enqueueMsg(RLCDownlinkMessage *);
	// The PDCH must feed the radio on time.  This is the routine that does it.
	void dlService();
	void transmit(RLCBSN_t bsn, BitVector *mI, const int *qbits, int transceiverflags);
	//void rollForward();
	//void mchResync();
	int findNeedyUSF();

	// Send the L2Frame down to the radio now.
	void send1Frame(BitVector& frame,ChannelCodingType encoding, bool idle);
	bool send1DataFrame(RLCDownEngine *tbfdown, RLCDownlinkDataBlock *block, int makeres,MsgTransactionType mttype,unsigned *pcounter);
	bool send1MsgFrame(TBF *tbf,RLCDownlinkMessage *msg, int makeres, MsgTransactionType mttype,unsigned *pcounter);
	void sendIdleFrame(RLCBSN_t bsn);
	void bugFixIdleFrame();
};

extern bool chCompareFunc(PDCHCommon*ch1, PDCHCommon*ch2);

}; // namespace GPRS


#endif
