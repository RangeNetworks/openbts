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
/**@file GPRS RLC-MAC state machine, GSM 04.60. */

/**@file GPRS RLC-MAC state machine, GSM 04.60. */


#ifndef GPRSL2RLCENGINE_H
#define GPRSL2RLCENGINE_H
#define INTERNAL_SGSN 1		// Use internal SGSN, no support for BSSG
#define UPLINK_PERSIST 1


#include <Interthread.h>
#include <list>

#include "GPRSInternal.h"
#if INTERNAL_SGSN==0
#include "BSSGMessages.h"
#endif
#include "TBF.h"
#define FAST_TBF 1			// Use aggregated downlink TBFs.

namespace GPRS {

class PDTCHL1FEC;

// Maximum LLC PDU size is 1560 bytes. (GSM04.60 sec9.1.12)  Bytes over are discarded in RLC.
// In unacknowledged mode, LLC-PDUs delivered in the order received, with 0-bits for missing blocks.
// The minimum payload size (using CS-1) is 20 bytes (see RLCPayloadSize)
// Therefore, a single PDU may take 78 blocks.
const int RLC_PDU_MAX_LEN = 1560;


//typedef std::list<BitVector*> LLCSegmentList;

class RLCEngineBase
{
	public:
	// WS => number of blocks sent simultaneously, awaiting ack, before blocking.
	// SNS => must be double WS, by definition.
	static const unsigned mWS = 64;		// Window Size
	static const unsigned mSNS = 128;		// Sequence Number Space

	/**@name SN arithmetic */
	int deltaSN(unsigned sn1, unsigned sn2) const;
	int deltaSNS(unsigned sn1, unsigned sn2) const;
	bool deltaEQ(unsigned sn1, unsigned sn2) const;
	unsigned addSN(int sn1, int sn2) const;	// Allow negative numbers.
	void incSN(unsigned &psn);
	virtual void engineDump(std::ostream &os) const = 0;
	string engineDumpStr() const { std::ostringstream os; engineDump(os); return os.str(); }
};

enum RlcUpState {
	RlcUpTransmit,
	RlcUpQuiescent,
	RlcUpPersistFinal,
	RlcUpFinished,
};

// (pat) This is an RLC endpoint as per GSM 04.60 9.1
// It sits in layer 2.
class RLCUpEngine : public TBF, public RLCEngineBase
{
	protected:
	/**@name RLC state variables, from GSM 04.60 9. */
	// We depend on the MS not to send us blocks with BSN > VQ+mWS, and inform
	// the MS of our concept of VQ in the acknack we send.
	// However, the only way we know the acknack is received may be that the MS
	// sends more blocks.
	struct {
		unsigned VR;		///< highest BSN received + 1 modulo wSNS;
							// 0 <= VR <= wSNS-1;
		unsigned VQ;		///< lowest BSN not yet received (window base)
							// In acknowledged mode, receive window is
							// defined by: VQ <= BSN <= VQ + mWS
		bool VN[mSNS];		///< receive status of previous RLC data blocks
		RLCUplinkDataBlock *RxQ[mSNS];	///< assembly queue for inbound RLC data blocks
		//unsigned RBSN;				///< BSN of incoming blocks.
	} mSt;
	//@}

#if INTERNAL_SGSN
	ByteVector *mUpPDU;	// The PDU being assembled.
#else
	BSSG::BSSGMsgULUnitData *mUpPDU;	// The PDU being assembled.
#endif
	Bool_z mIncompletePDU;	// Special case: If set, the PDU did not finish in this TBF;
							// MS needs another TBF to send the rest of it.
							// Dont think we need this if using unlimited dynamic mode uplink.
	Bool_z mUpStalled;			// MS is stalled, copied from each uplink block header.

	static const unsigned mNumUpPerAckNack = 10;
	UInt_z mNumUpBlocksSinceAckNack;		// Number of blocks sent since the last acknack
	UInt_z mTotalBlocksReceived;
	UInt_z mUniqueBlocksReceived;
	int mBytesPending;
#if UPLINK_PERSIST
	RLCBSN_t mDataPersistFinalEndBSN;	// When DataPersistFinal mode started.
	bool mUpPersistentMode;
	GprsTimer mtUpKeepAliveTimer;	// Time to next keep alive.
	GprsTimer mtUpPersistTimer; 	// How long TBF persists while idle.
#endif
	
	// The MS keeps a running total of USF slots granted to the MS.
	// The difference between the current value and the starting value divided
	// by the number of unique blocks received is a measure of the loss ratio,
	// although that breaks down if there are multiple simultaneous uplink TBFs.
	unsigned mStartUsfGrants;
	//unsigned mNumServiceSlotsSkipped;	// Reality check disaster recovery.
	RlcUpState mtUpState;			// Set after all blocks have been received.

	public:
	RLCUpEngine(MSInfo *wms,int wOctetCount);
	~RLCUpEngine();

	void addUpPDU(BitVector&);
	void sendPDU();
	bool stalled() const { return mUpStalled; }
#if UPLINK_PERSIST
	bool ulPersistentMode();
	bool sendNonFinalAckNack(PDCHL1Downlink *down);
#endif

	void engineRecvDataBlock(RLCUplinkDataBlock* block, int tn);
	void engineUpAdvanceWindow();
	bool engineService(PDCHL1Downlink *down);	// Thats right: we pass the downlink.
	float engineDesiredUtilization();
	void engineDump(std::ostream &os) const;
	void engineGetStats(unsigned *pSlotsTotal, unsigned *pSlotsUsed, unsigned *pGrants) const {
		*pSlotsTotal = mTotalBlocksReceived;
		*pSlotsUsed = mUniqueBlocksReceived;
		*pGrants = mtMS->msNumDataUSFGrants - mStartUsfGrants;
	}
	int engineGetBytesPending() { return mBytesPending; }
	RLCMsgPacketUplinkAckNack * engineUpAckNack();
	TBF *getTBF() { return dynamic_cast<TBF*>(this);}
};

class RLCDownEngine : public TBF, public RLCEngineBase
{
	public:

	/**@name RLC state variables, from GSM 04.60 9. */
	//@{
	struct {
		unsigned VS;		///< BSN of next RLC data block for tx
						// After receipt of acknack message, VS is set back to VA.
		unsigned VA;		///< BSN of oldest un-acked RLC data block
		bool VB[mSNS];		///< ack status of pending RLC data blocks (true = acked)
		RLCDownlinkDataBlock *TxQ[mSNS];	///< unacked RLC data blocks saved for re-tx
		//int sendTime[mSNS];	///<RLCBSN when block was first sent.
		// VCS is used for multi-block Control Messages, so does not apply to us.
		// bool VCS;			///< 0 or 1 indicating state for multi-block RLC control messages.
		unsigned TxQNum;		// One greater than last block in queue.  It wraps around.
	} mSt;
	//@}
	// This is additional state:
	UInt_z mPrevAckSsn;		// The SSN of the previous downlinkAckNack.
	UInt_z mResendSsn;		// Resend blocks with SN less than this.
	UInt_z mPrevAckBlockCount;	// Saves value of mTotalBlocksSent from last downlinkAckNack.

	Bool_z mDownStalled;	// Yes, set when the downlink is stalled.
	Bool_z mDownFinished;	// Set when we have sent the block with the FBI indicator.
	Bool_z mAllAcked;		// Have all the blocks been acked?
	UInt_z mNumDownBlocksSinceAckNack;
	unsigned mNumDownPerAckNack;
	UInt_z mTotalBlocksSent;		// Total sent; some of them may have been lost in transmission
	UInt_z mTotalDataBlocksSent;	// Number of blocks in the TBF.
	UInt_z mUniqueDataBlocksSent;	// Number of blocks in the TBF.

#if INTERNAL_SGSN==0
	// BSSG no longer used:
	BSSG::BSSGMsgDLUnitData  *mBSSGDlMsg;	// The downlink message that created this TBF.
#endif
								// We save it because the RLCEngine has a pointer into it,
								// and delete it when we delete the TBF.
	ByteVector mDownPDU;	// The PDU part of mBSSGDlMsg.  Automatic destruction.
	SGSN::GprsSgsnDownlinkPdu *mDownlinkPdu;	// This is the most recent sdu sent in the TBF.
								// We keep it around so we can retry the TBF if necessary.

	// Moved to class TBF:
	GprsTimer mtDownKeepAliveTimer;	// Time to next keep alive.
	GprsTimer mtDownPersistTimer; 	// How long TBF persists while idle.


	RLCDownEngine(MSInfo *wms) :
		TBF(wms,RLCDir::Down),
#if INTERNAL_SGSN==0
		mBSSGDlMsg(0),	// No longer used.
#endif
		mDownlinkPdu(0)
	{
		memset(&mSt,0,sizeof(mSt));
		mNumDownPerAckNack = gConfig.getNum("GPRS.TBF.Downlink.Poll1");
	}
	~RLCDownEngine();

	// Is the downlink engine stalled, waiting for acknack messages?
	bool stalled() const { return mDownStalled; }
	//bool finished() { return mSt.VS >= mSt.TxQNum; }
	unsigned engineDownPDUSize() const { return mDownPDU.size(); }

	RLCDownlinkDataBlock *getBlock(unsigned vs,int tn);
	void engineWriteHighSide(SGSN::GprsSgsnDownlinkPdu *dlmsg);	// TODO: get rid of this.
	bool dlPersistentMode();	// Are we using persistent TBF mode?
	bool dataAvail();		// Is more downlink data available?
	RLCDownlinkDataBlock* engineFillBlock(unsigned bsn,int tn);
	void engineRecvAckNack(const RLCMsgPacketDownlinkAckNack*);
	bool engineService(PDCHL1Downlink *down);
	float engineDesiredUtilization();
	void engineDump(std::ostream &os) const;
	void engineGetStats(unsigned *pSlotsTotal, unsigned *pSlotsUsed, unsigned *pGrants) const {
		// We dont care about the control blocks sent, only the data.
		//*pSlotsTotal = mTotalBlocksSent;	// Number of blocks we have sent.
		*pSlotsTotal = mTotalDataBlocksSent;	// Number of blocks we have sent.
		*pSlotsUsed = mUniqueDataBlocksSent;	// Number of blocks MS has acknowledged.
		*pGrants = 0;	// Inapplicable to downlink.
	}
	int engineGetBytesPending() { return 0; }	// TODO, but it is a negligible amount
	//bool isLastUABlock();
	//unsigned blocksToGo();
	bool resendNeeded(int bsn);
	void advanceVS();
	void advanceVA();
	TBF *getTBF() { return dynamic_cast<TBF*>(this);}
};



// This incorporates both an up and down engine, but only one of them is used.
//class RLCEngine :
//	public RLCUpEngine,
//	public RLCDownEngine
//{
//	public:
//	RLCEngine(TBF *);
//	~RLCEngine();
//};


}; // namespace GPRS



#endif
