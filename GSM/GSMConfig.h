/*
* Copyright 2008-2010 Free Software Foundation, Inc.
* Copyright 2010 Kestrel Signal Processing, Inc.
* Copyright 2012 Range Networks, Inc.
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



#ifndef GSMCONFIG_H
#define GSMCONFIG_H

#include "Defines.h"
#include <vector>
#include <Interthread.h>

//#include <ControlCommon.h>
#include <RadioResource.h>
#include <PowerManager.h>

#include "GSML3RRElements.h"
#include "GSML3CommonElements.h"
#include "GSML3RRMessages.h"

#include "TRXManager.h"


namespace GSM {

// From GSM 05.02 6.5.
const unsigned sMax_BS_PA_MFRMS = 9;


class CCCHLogicalChannel;
class SDCCHLogicalChannel;
class CBCHLogicalChannel;
class TCHFACCHLogicalChannel;

class CCCHList : public std::vector<CCCHLogicalChannel*> {};
class SDCCHList : public std::vector<SDCCHLogicalChannel*> {};
class TCHList : public std::vector<TCHFACCHLogicalChannel*> {};

/**
	This object carries the top-level GSM air interface configuration.
	It serves as a central clearinghouse to get access to everything else in the GSM code.
*/
class GSMConfig {

	private:

	/** The paging mechanism is built-in. */
	Control::Pager mPager;

	PowerManager mPowerManager;

	mutable Mutex mLock;						///< multithread access control

	/**@name Groups of CCCH subchannels -- may intersect. */
	//@{
	CCCHList mAGCHPool;		///< access grant CCCH subchannels
	CCCHList mPCHPool;		///< paging CCCH subchannels
	//@}

	CBCHLogicalChannel* mCBCH;

	/**@name Allocatable channel pools. */
	//@{
	SDCCHList mSDCCHPool;
	TCHList mTCHPool;
	//@}

	/**@name BSIC. */
	//@{
	unsigned mNCC;		///< network color code
	unsigned mBCC;		///< basestation color code
	//@}

	GSMBand mBand;		///< BTS operating band, or 0 for custom band

	Clock mClock;		///< local copy of BTS master clock

	/**@name Encoded L2 frames to be sent on the BCCH. */
	//@{
	L2Frame mSI1Frame;
	L2Frame mSI2Frame;
	L2Frame mSI3Frame;
	L2Frame mSI4Frame;
	L2Frame mSI13Frame;	// pat added for GPRS
	//@}

	/**@name Encoded L3 frames to be sent on the SACCH. */
	//@{
	L3Frame mSI5Frame;
	L3Frame mSI6Frame;
	//@}

	/**@name Copies of system information messages as they were most recently generated. */
	//@{
	L3SystemInformationType1* mSI1;
	L3SystemInformationType2* mSI2;
	L3SystemInformationType3* mSI3;
	L3SystemInformationType4* mSI4;
	L3SystemInformationType5* mSI5;
	L3SystemInformationType6* mSI6;
	//@}

	int mT3122;

	time_t mStartTime;

	L3LocationAreaIdentity mLAI;

	bool mHold;		///< If true, do not respond to RACH bursts.

	InterthreadQueue<Control::ChannelRequestRecord> mChannelRequestQueue;
	Thread mAccessGrantThread;

	unsigned mChangemark;



	void crackPagingFromImsi(unsigned imsiMod1000,unsigned &ccch_group,unsigned &paging_Index);;

	public:
	
	

	GSMConfig();

	/** Initialize with parameters from gConfig.  */
	void init();

	/** Start the internal control loops. */
	void start();
	
	/**@name Get references to L2 frames for BCCH SI messages. */
	//@{
	const L2Frame& SI1Frame() const { return mSI1Frame; }
	const L2Frame& SI2Frame() const { return mSI2Frame; }
	const L2Frame& SI3Frame() const { return mSI3Frame; }
	const L2Frame& SI4Frame() const { return mSI4Frame; }
	const L2Frame& SI13Frame() const { return mSI13Frame; }	// pat added for GPRS
	//@}
	/**@name Get references to L3 frames for SACCH SI messages. */
	//@{
	const L3Frame& SI5Frame() const { return mSI5Frame; }
	const L3Frame& SI6Frame() const { return mSI6Frame; }
	//@}
	/**@name Get the messages themselves. */
	//@{
	const L3SystemInformationType1* SI1() const { return mSI1; }
	const L3SystemInformationType2* SI2() const { return mSI2; }
	const L3SystemInformationType3* SI3() const { return mSI3; }
	const L3SystemInformationType4* SI4() const { return mSI4; }
	const L3SystemInformationType5* SI5() const { return mSI5; }
	const L3SystemInformationType6* SI6() const { return mSI6; }
	//@}

	/** Get the current master clock value. */
	Time time() const { return mClock.get(); }

	/**@name Accessors. */
	//@{
	Control::Pager& pager() { return mPager; }
	GSMBand band() const { return mBand; }
	unsigned BCC() const { return mBCC; }
	unsigned NCC() const { return mNCC; }
	GSM::Clock& clock() { return mClock; }
	const L3LocationAreaIdentity& LAI() const { return mLAI; }
	unsigned changemark() const { return mChangemark; }
	//@}

	/** Return the BSIC, NCC:BCC. */
	unsigned BSIC() const { return (mNCC<<3) | mBCC; }

	/**
		Re-encode the L2Frames for system information messages.
		Called whenever a beacon parameter is changed.
	*/
	void regenerateBeacon();

	/**
		SI5 is generated separately because it may get random
		neighbors added each time it's sent.
	*/
	void regenerateSI5();

	/**
		Hold off on channel allocations; don't answer RACH.
		@param val true to hold, false to clear hold
	*/
	void hold(bool val);

	/**
		Return true if we are holding off channel allocation.
	*/
	bool hold() const;

	protected:

	/** Find a minimum-load CCCH from a list. */
	CCCHLogicalChannel* minimumLoad(CCCHList &chanList);

	/** Return the total load of a CCCH list. */
	size_t totalLoad(const CCCHList &chanList) const;

	public:

	size_t AGCHLoad() { return totalLoad(mAGCHPool); }
	size_t PCHLoad() { return totalLoad(mPCHPool); }

	/**@name Manage CCCH subchannels. */
	//@{

	/** The add method is not mutex protected and should only be used during initialization. */
	void addAGCH(CCCHLogicalChannel* wCCCH) { mAGCHPool.push_back(wCCCH); }

	/** The add method is not mutex protected and should only be used during initialization. */
	void addPCH(CCCHLogicalChannel* wCCCH) { mPCHPool.push_back(wCCCH); }

	/** Return a minimum-load AGCH. */
	// (pat) TODO: This strategy needs to change.
	// There needs to be a common message queue for all CCCH timeslots from which the
	// FEC can pull the next AGCH message if there is no paging message at that paging slot.
	// And if someone besides pat works on this, note that gprs also wants
	// to be able cancel messages after sending them in case conditions have changed,
	// and also needs to know, a-priori, the exact frame number when the message
	// is going to be sent, none of which works properly at the moment.
	CCCHLogicalChannel* getAGCH() { return minimumLoad(mAGCHPool); }

#if ENABLE_PAGING_CHANNELS
	///< (pat) Send a paging message for the specified imsi.
	// This function should be used instead of getPCH(), etc. which should then be made private.
	void sendPCH(const L3RRMessage& msg,unsigned imsiMod1000);

	///< (pat) Return the approximate time of the next PCH message for this imsi.
	// This routine should be elided after DRX mode in GPRS is fixed.
	Time getPchSendTime(imsiMod1000);

	///< (pat) Send a message on the avail AGCH.
	void sendAGCH(const L3RRMessage& msg);
#endif

	/** Return a minimum-load PCH. */
	CCCHLogicalChannel* getPCH() { return minimumLoad(mPCHPool); }

	/** Return a specific PCH. */
	CCCHLogicalChannel* getPCH(size_t index)
	{
		assert(index<mPCHPool.size());
		return mPCHPool[index];
	}

	/** Return the number of configured AGCHs */
	unsigned numAGCHs() const { return mAGCHPool.size(); }

	/** Enqueue a RACH channel request; to be deleted when dequeued later. */
	void channelRequest(Control::ChannelRequestRecord *req)
		{ mChannelRequestQueue.write(req); }

	Control::ChannelRequestRecord* nextChannelRequest()
		{ return mChannelRequestQueue.read(); }

	void flushChannelRequests()
		{ mChannelRequestQueue.clear(); }

	//@}


	/**@ Manage the CBCH. */
	//@{

	/** The add method is not mutex protected and should only be used during initialization. */
	void addCBCH(CBCHLogicalChannel *wCBCH)
		{ assert(mCBCH==NULL); mCBCH=wCBCH; }

	CBCHLogicalChannel* getCBCH() { return mCBCH; }
	//@}


	/**@name Manage SDCCH Pool. */
	//@{
	/** The add method is not mutex protected and should only be used during initialization. */
	void addSDCCH(SDCCHLogicalChannel *wSDCCH) { mSDCCHPool.push_back(wSDCCH); }
	/** Return a pointer to a usable channel. */
	SDCCHLogicalChannel *getSDCCH();
	/** Return true if an SDCCH is available, but do not allocate it. */
	size_t SDCCHAvailable() const;
	/** Return number of total SDCCH. */
	unsigned SDCCHTotal() const { return mSDCCHPool.size(); }
	/** Return number of active SDCCH. */
	unsigned SDCCHActive() const;
	/** Just a reference to the SDCCH pool. */
	const SDCCHList& SDCCHPool() const { return mSDCCHPool; }
	//@}

	/**@name Manage TCH pool. */
	//@{
	/** The add method is not mutex protected and should only be used during initialization. */
	void addTCH(TCHFACCHLogicalChannel *wTCH) { mTCHPool.push_back(wTCH); }
	/** Return a pointer to a usable channel. */
	TCHFACCHLogicalChannel *getTCH(bool forGPRS=false, bool onlyCN0=false);
	int getTCHGroup(int groupSize,TCHFACCHLogicalChannel **results);
	/** Return true if an TCH is available, but do not allocate it. */
	size_t TCHAvailable() const;
	/** Return number of total TCH. */
	unsigned TCHTotal() const;
	/** Return number of active TCH. */
	unsigned TCHActive() const;
	/** Just a reference to the TCH pool. */
	const TCHList& TCHPool() const { return mTCHPool; }
	//@}

	/**@name T3122 management */
	//@{
	unsigned T3122() const;
	unsigned growT3122();
	unsigned shrinkT3122();
	//@}

	/**@name Methods to create channel combinations. */
	//@{
	/** Combination 0 is a idle slot, as opposed to a non-transmitting one. */
	void createCombination0(TransceiverManager &TRX, unsigned TN);
	/** Combination I is full rate traffic. */
	void createCombinationI(TransceiverManager &TRX, unsigned CN, unsigned TN);
	/** Combination VII is 8 SDCCHs. */
	void createCombinationVII(TransceiverManager &TRX, unsigned CN, unsigned TN);
	/** Combination XIII is a GPRS PDTCH: PDTCH/F+PACCH/F+PTCCH/F */
	// pat todo: This does not exist yet.
	void createCombinationXIII(TransceiverManager &TRX, unsigned CN, unsigned TN);
	//@}

	/** Return number of seconds since starting. */
	time_t uptime() const { return ::time(NULL)-mStartTime; }

	/** Get a handle to the power manager. */
	PowerManager& powerManager() { return mPowerManager; }
};



};	// GSM


/**@addtogroup Globals */
//@{
/** A single global GSMConfig object in the global namespace. */
extern GSM::GSMConfig gBTS;
//@}


#endif


// vim: ts=4 sw=4
