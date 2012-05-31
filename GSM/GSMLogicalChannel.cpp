/**@file Logical Channel.  */

/*
* Copyright 2008, 2009, 2010, 2011 Free Software Foundation, Inc.
* Copyright 2010 Kestrel Signal Processing, Inc.
* Copyright 2011 Range Networks, Inc.
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



#include "GSML3RRElements.h"
#include "GSML3Message.h"
#include "GSML3RRMessages.h"
#include "GSMLogicalChannel.h"
#include "GSMConfig.h"

#include <TransactionTable.h>
#include <SMSControl.h>
#include <ControlCommon.h>

#include <Logger.h>
#undef WARNING

using namespace std;
using namespace GSM;



void LogicalChannel::open()
{
	LOG(INFO);
	if (mSACCH) mSACCH->open();
	if (mL1) mL1->open();
	for (int s=0; s<4; s++) {
		if (mL2[s]) mL2[s]->open();
	}
	// Empty any stray transactions in the FIFO from the SIP layer.
	while (true) {
		Control::TransactionEntry *trans = mTransactionFIFO.readNoBlock();
		if (!trans) break;
		LOG(WARNING) << "flushing stray transaction " << *trans;
	}
}


void LogicalChannel::connect()
{
	mMux.downstream(mL1);
	if (mL1) mL1->upstream(&mMux);
	for (int s=0; s<4; s++) {
		mMux.upstream(mL2[s],s);
		if (mL2[s]) mL2[s]->downstream(&mMux);
	}
}


void LogicalChannel::downstream(ARFCNManager* radio)
{
	assert(mL1);
	mL1->downstream(radio);
	if (mSACCH) mSACCH->downstream(radio);
}



// Serialize and send an L3Message with a given primitive.
void LogicalChannel::send(const L3Message& msg,
		const GSM::Primitive& prim,
		unsigned SAPI)
{
	LOG(INFO) << "L3 SAP" << SAPI << " sending " << msg;
	send(L3Frame(msg,prim), SAPI);
}




CCCHLogicalChannel::CCCHLogicalChannel(const TDMAMapping& wMapping)
	:mRunning(false)
{
	mL1 = new CCCHL1FEC(wMapping);
	mL2[0] = new CCCHL2;
	connect();
}


void CCCHLogicalChannel::open()
{
	LogicalChannel::open();
	if (!mRunning) {
		mRunning=true;
		mServiceThread.start((void*(*)(void*))CCCHLogicalChannelServiceLoopAdapter,this);
	}
}


void CCCHLogicalChannel::serviceLoop() 
{
	// build the idle frame
	static const L3PagingRequestType1 filler;
	static const L3Frame idleFrame(filler,UNIT_DATA);
	// prime the first idle frame
	LogicalChannel::send(idleFrame);
	// run the loop
	while (true) {
		L3Frame* frame = mQ.read();
		if (frame) {
			LogicalChannel::send(*frame);
			OBJLOG(DEBUG) << "CCCHLogicalChannel::serviceLoop sending " << *frame;
			delete frame;
		}
		if (mQ.size()==0) {
			LogicalChannel::send(idleFrame);
			OBJLOG(DEBUG) << "CCCHLogicalChannel::serviceLoop sending idle frame";
		}
	}
}


void *GSM::CCCHLogicalChannelServiceLoopAdapter(CCCHLogicalChannel* chan)
{
	chan->serviceLoop();
	return NULL;
}



L3ChannelDescription LogicalChannel::channelDescription() const
{
	// In some debug cases, L1 may not exist, so we fake this information.
	if (mL1==NULL) return L3ChannelDescription(TDMA_MISC,0,0,0);

	// In normal cases, we get this information from L1.
	return L3ChannelDescription(
		mL1->typeAndOffset(),
		mL1->TN(),
		mL1->TSC(),
		mL1->ARFCN()
	);
}




SDCCHLogicalChannel::SDCCHLogicalChannel(
		unsigned wCN,
		unsigned wTN,
		const CompleteMapping& wMapping)
{
	mL1 = new SDCCHL1FEC(wCN,wTN,wMapping.LCH());
	// SAP0 is RR/MM/CC, SAP3 is SMS
	// SAP1 and SAP2 are not used.
	L2LAPDm *SAP0L2 = new SDCCHL2(1,0);
	L2LAPDm *SAP3L2 = new SDCCHL2(1,3);
	LOG(DEBUG) << "LAPDm pairs SAP0=" << SAP0L2 << " SAP3=" << SAP3L2;
	SAP3L2->master(SAP0L2);
	mL2[0] = SAP0L2;
	mL2[3] = SAP3L2;
	mSACCH = new SACCHLogicalChannel(wCN,wTN,wMapping.SACCH());
	connect();
}





SACCHLogicalChannel::SACCHLogicalChannel(
		unsigned wCN,
		unsigned wTN,
		const MappingPair& wMapping)
		: mRunning(false)
{
	mSACCHL1 = new SACCHL1FEC(wCN,wTN,wMapping);
	mL1 = mSACCHL1;
	// SAP0 is RR, SAP3 is SMS
	// SAP1 and SAP2 are not used.
	mL2[0] = new SACCHL2(1,0);
	mL2[3] = new SACCHL2(1,3);
	connect();
	assert(mSACCH==NULL);
}


void SACCHLogicalChannel::open()
{
	LogicalChannel::open();
	if (!mRunning) {
		mRunning=true;
		mServiceThread.start((void*(*)(void*))SACCHLogicalChannelServiceLoopAdapter,this);
	}
}



L3Message* processSACCHMessage(L3Frame *l3frame)
{
	if (!l3frame) return NULL;
	LOG(DEBUG) << *l3frame;
	Primitive prim = l3frame->primitive();
	if ((prim!=DATA) && (prim!=UNIT_DATA)) {
		LOG(INFO) << "non-data primitive " << prim;
		return NULL;
	}
	// FIXME -- Why, again, do we need to do this?
//	L3Frame realFrame = l3frame->segment(24, l3frame->size()-24);
	L3Message* message = parseL3(*l3frame);
	if (!message) {
		LOG(WARNING) << "SACCH recevied unparsable L3 frame " << *l3frame;
	}
	return message;
}


void SACCHLogicalChannel::serviceLoop()
{
	// run the loop
	unsigned count = 0;
	while (true) {

		// Throttle back if not active.
		if (!active()) {
			OBJLOG(DEBUG) << "SACCH sleeping";
			sleepFrames(51);
			continue;
		}

		// TODO SMS -- Check to see if the tx queues are empty.  If so, send SI5/6,
		// otherwise sleep and continue;

		// Send alternating SI5/SI6.
		OBJLOG(DEBUG) << "sending SI5/6 on SACCH";
		if (count%2) LogicalChannel::send(gBTS.SI5Frame());
		else LogicalChannel::send(gBTS.SI6Frame());
		count++;

		// Receive inbound messages.
		// This read loop flushes stray reports quickly.
		while (true) {

			OBJLOG(DEBUG) << "polling SACCH for inbound messages";
			bool nothing = true;

			// Process SAP0 -- RR Measurement reports
			L3Frame *rrFrame = LogicalChannel::recv(0,0);
			if (rrFrame) nothing=false;
			L3Message* rrMessage = processSACCHMessage(rrFrame);
			delete rrFrame;
			if (rrMessage) {
				L3MeasurementReport* measurement = dynamic_cast<L3MeasurementReport*>(rrMessage);
				if (measurement) {
					mMeasurementResults = measurement->results();
					OBJLOG(DEBUG) << "SACCH measurement report " << mMeasurementResults;
					// Add the measurement results to the table
					// Note that the typeAndOffset of a SACCH match the host channel.
					gPhysStatus.setPhysical(this, mMeasurementResults);
				} else {
					OBJLOG(NOTICE) << "SACCH SAP0 sent unaticipated message " << rrMessage;
				}
				delete rrMessage;
			}

			// Process SAP3 -- SMS
			L3Frame *smsFrame = LogicalChannel::recv(0,3);
			if (smsFrame) nothing=false;
			L3Message* smsMessage = processSACCHMessage(smsFrame);
			delete smsFrame;
			if (smsMessage) {
				const SMS::CPData* cpData = dynamic_cast<const SMS::CPData*>(smsMessage);
				if (cpData) {
					OBJLOG(INFO) << "SMS CPDU " << *cpData;
					Control::TransactionEntry *transaction = gTransactionTable.find(this);
					try {
						if (transaction) {
							Control::InCallMOSMSController(cpData,transaction,this);
						} else {
							OBJLOG(WARNING) << "in-call MOSMS CP-DATA with no corresponding transaction";
						}
					} catch (Control::ControlLayerException e) {
						//LogicalChannel::send(RELEASE,3);
						gTransactionTable.remove(e.transactionID());
					}
				} else {
					OBJLOG(NOTICE) << "SACCH SAP3 sent unaticipated message " << rrMessage;
				}
				delete smsMessage;
			}

			// Anything from the SIP side?
			// MTSMS (delivery from SIP to the MS)
			Control::TransactionEntry *sipTransaction = mTransactionFIFO.readNoBlock();
			if (sipTransaction) {
				OBJLOG(INFO) << "SIP-side transaction: " << sipTransaction;
				assert(sipTransaction->service() == L3CMServiceType::MobileTerminatedShortMessage);
				try {
					Control::MTSMSController(sipTransaction,this);
				} catch (Control::ControlLayerException e) {
					//LogicalChannel::send(RELEASE,3);
					gTransactionTable.remove(e.transactionID());
				}
			}

			// Nothing happened?
			if (nothing) break;
		}

	}
}


void *GSM::SACCHLogicalChannelServiceLoopAdapter(SACCHLogicalChannel* chan)
{
	chan->serviceLoop();
	return NULL;
}


// These have to go into the .cpp file to prevent an illegal forward reference.
void LogicalChannel::setPhy(float wRSSI, float wTimingError)
	{ assert(mSACCH); mSACCH->setPhy(wRSSI,wTimingError); }
void LogicalChannel::setPhy(const LogicalChannel& other)
	{ assert(mSACCH); mSACCH->setPhy(*other.SACCH()); }
float LogicalChannel::RSSI() const
	{ assert(mSACCH); return mSACCH->RSSI(); }
float LogicalChannel::timingError() const
	{ assert(mSACCH); return mSACCH->timingError(); }
int LogicalChannel::actualMSPower() const
	{ assert(mSACCH); return mSACCH->actualMSPower(); }
int LogicalChannel::actualMSTiming() const
	{ assert(mSACCH); return mSACCH->actualMSTiming(); }



TCHFACCHLogicalChannel::TCHFACCHLogicalChannel(
		unsigned wCN,
		unsigned wTN,
		const CompleteMapping& wMapping)
{
	mTCHL1 = new TCHFACCHL1FEC(wCN,wTN,wMapping.LCH());
	mL1 = mTCHL1;
	// SAP0 is RR/MM/CC, SAP3 is SMS
	// SAP1 and SAP2 are not used.
	mL2[0] = new FACCHL2(1,0);
	mL2[3] = new FACCHL2(1,3);
	mSACCH = new SACCHLogicalChannel(wCN,wTN,wMapping.SACCH());
	connect();
}






bool LogicalChannel::waitForPrimitive(Primitive primitive, unsigned timeout_ms)
{
	bool waiting = true;
	while (waiting) {
		L3Frame *req = recv(timeout_ms);
		if (req==NULL) {
			LOG(NOTICE) << "timeout at uptime " << gBTS.uptime() << " frame " << gBTS.time();
			return false;
		}
		waiting = (req->primitive()!=primitive);
		delete req;
	}
	return true;
}


void LogicalChannel::waitForPrimitive(Primitive primitive)
{
	bool waiting = true;
	while (waiting) {
		L3Frame *req = recv();
		if (req==NULL) continue;
		waiting = (req->primitive()!=primitive);
		delete req;
	}
}


ostream& GSM::operator<<(ostream& os, const LogicalChannel& chan)
{
	os << chan.descriptiveString();
	return os;
}


void LogicalChannel::addTransaction(Control::TransactionEntry *transaction)
{
	assert(transaction->channel()==this);
	mTransactionFIFO.write(transaction);
}

// vim: ts=4 sw=4

