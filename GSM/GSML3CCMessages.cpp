/** @file Call Control messags, GSM 04.08 9.3.  */

/*
* Copyright 2008, 2009, 2011 Free Software Foundation, Inc.
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





#include <iostream>
#include "GSML3CCMessages.h"
#include <Logger.h>



using namespace std;
using namespace GSM;



ostream& GSM::operator<<(ostream& os, L3CCMessage::MessageType val)
{
	switch (val) {
		case L3CCMessage::Alerting: 
				os << "Alerting"; break;
		case L3CCMessage::Connect:
				os << "Connect"; break;
		case L3CCMessage::Disconnect:
				os << "Disconnect"; break;
		case L3CCMessage::ConnectAcknowledge: 
				os << "Connect Acknowledge"; break;
		case L3CCMessage::Release: 
				os << "Release"; break;		  
		case L3CCMessage::ReleaseComplete: 
				os << "Release Complete"; break;		  
		case L3CCMessage::Setup: 
				os << "Setup"; break;
		case L3CCMessage::EmergencySetup:
				os << "Emergency Setup"; break;
		case L3CCMessage::CCStatus: 
				os <<"CC Status"; break;
		case L3CCMessage::CallConfirmed: 
				os <<"Call Confirmed"; break;
		case L3CCMessage::CallProceeding: 
				os <<"Call Proceeding"; break;
		case L3CCMessage::StartDTMF:
				os << "Start DTMF"; break;
		case L3CCMessage::StartDTMFReject:
				os << "Start DTMF Reject"; break;
		case L3CCMessage::StartDTMFAcknowledge:
				os << "Start DTMF Acknowledge"; break;
		case L3CCMessage::StopDTMF:
				os << "Stop DTMF"; break;
		case L3CCMessage::StopDTMFAcknowledge:
				os << "Stop DTMF Acknowledge"; break;
		case L3CCMessage::Hold:
				os << "Hold"; break;
		case L3CCMessage::HoldReject:
				os << "Hold Reject"; break;
		default: os << hex << "0x" << (int)val << dec;
	}
	return os;
}



L3CCMessage * GSM::L3CCFactory(L3CCMessage::MessageType MTI)
{
	switch (MTI) {
		case L3CCMessage::Connect: return new L3Connect();
		case L3CCMessage::Alerting: return new L3Alerting();
		case L3CCMessage::Setup: return new L3Setup();
		case L3CCMessage::EmergencySetup: return new L3EmergencySetup();
		case L3CCMessage::Disconnect: return new L3Disconnect();
		case L3CCMessage::CallProceeding: return new L3CallProceeding();
		case L3CCMessage::Release: return new L3Release();
		case L3CCMessage::ReleaseComplete: return new L3ReleaseComplete();
		case L3CCMessage::ConnectAcknowledge: return new L3ConnectAcknowledge();
		case L3CCMessage::CCStatus: return new L3CCStatus();
		case L3CCMessage::CallConfirmed: return new L3CallConfirmed();
		case L3CCMessage::StartDTMF: return new L3StartDTMF();
		case L3CCMessage::StopDTMF: return new L3StopDTMF();
		case L3CCMessage::Hold: return new L3Hold();
		default: {
			LOG(NOTICE) << "no L3 CC factory support for message "<< MTI;
			return NULL;
		}
	}
}




/* parser for Call control messages, will only parse uplink */
L3CCMessage * GSM::parseL3CC(const L3Frame& source)
{
    // mask out bit #7 (1011 1111) so use 0xbf, see GSM 04.08 Table 10.3/3.
    L3CCMessage::MessageType MTI = (L3CCMessage::MessageType)(0xbf & source.MTI());
	LOG(DEBUG) << "MTI="<<MTI;

	L3CCMessage *retVal = L3CCFactory(MTI);
	if (retVal==NULL) return NULL;

	retVal->TI(source.TI());
	retVal->parse(source);
	LOG(DEBUG) << *retVal;
	return retVal;
}


void L3CCMessage::write(L3Frame& dest) const
{
	// We override L3Message::write for the transaction identifier.
	size_t l3len = bitsNeeded();
	if (dest.size()!=l3len) dest.resize(l3len);
	size_t wp = 0;
	dest.writeField(wp,mTI,4);
	dest.writeField(wp,PD(),4);
	dest.writeField(wp,MTI(),8);

	writeBody(dest,wp);
}




void L3CCMessage::text(ostream& os) const
{
	os << "CC " << (MessageType) MTI();
	os << " TI=" << mTI << " ";
}


size_t L3Alerting::l2BodyLength() const
{
	size_t sum=0;
	if (mHaveProgress) sum += mProgress.lengthTLV();
	return sum;
}

void L3Alerting::writeBody(L3Frame &dest, size_t &wp) const
{
	if (mHaveProgress) mProgress.writeTLV(0x1E,dest,wp);
}

void L3Alerting::parseBody(const L3Frame& src, size_t &rp)
{
	skipTLV(0x1C,src,rp);		// skip facility
	mHaveProgress = mProgress.parseTLV(0x1E,src,rp);
	// ignore the rest
}

void L3Alerting::text(ostream& os) const
{
	L3CCMessage::text(os);
	if (mHaveProgress) os << "progress=(" << mProgress << ")";
}


size_t L3CallProceeding::l2BodyLength() const
{
	size_t sum=0;
	if (mHaveProgress) sum += mProgress.lengthTLV();
	if( mHaveBearerCapability) sum += mBearerCapability.lengthTLV();
	return sum;
}


void L3CallProceeding::writeBody(L3Frame &dest, size_t &wp) const
{
	if( mHaveBearerCapability) mBearerCapability.writeTLV(0x04, dest, wp);
	if (mHaveProgress) mProgress.writeTLV(0x1E, dest, wp);

}

void L3CallProceeding::parseBody(const L3Frame& src, size_t &rp)
{
	skipTV(0x0D,4,src,rp);		// skip repeat indicator
	skipTLV(0x04,src,rp);		// skip bearer capability 1
	skipTLV(0x04,src,rp);		// skip bearer capability 2
	skipTLV(0x1C,src,rp);		// skip facility
	mHaveProgress = mProgress.parseTLV(0x1E,src,rp);
}

void L3CallProceeding::text(ostream& os) const
{
	L3CCMessage::text(os);
	if (mHaveProgress) os << "progress=(" << mProgress << ")";
}






size_t L3Connect::l2BodyLength() const
{
	size_t len=0;
	if (mHaveProgress) len += mProgress.lengthTLV();
	return len;
}

void L3Connect::writeBody(L3Frame &dest, size_t &wp) const
{
	if (mHaveProgress) mProgress.writeTLV(0x1E,dest,wp);
}

void L3Connect::parseBody(const L3Frame& src, size_t &rp)
{
	skipTLV(0x1c,src,rp);		// facility
	mHaveProgress = mProgress.parseTLV(0x1e,src,rp);
	// ignore the rest
}


void L3Connect::text(ostream& os) const
{
	L3CCMessage::text(os);
	if (mHaveProgress) os << "progress=(" << mProgress << ")";
}




size_t L3Release::l2BodyLength() const
{
	size_t sum = 0;
	if (mHaveCause) sum += mCause.lengthTLV();
	return sum;
}


void L3Release::writeBody(L3Frame& dest, size_t &wp) const
{
	if (mHaveCause) mCause.writeTLV(0x08,dest,wp);
}


void L3Release::parseBody(const L3Frame& src, size_t &rp)
{
	mHaveCause = mCause.parseTLV(0x08,src,rp);
	// ignore the rest
}


void L3Release::text(ostream& os) const
{
	L3CCMessage::text(os);
	if (mHaveCause) os << "cause=(" << mCause << ")";
}



size_t L3ReleaseComplete::l2BodyLength() const
{
	size_t sum = 0;
	if (mHaveCause) sum += mCause.lengthTLV();
	return sum;
}


void L3ReleaseComplete::writeBody(L3Frame& dest, size_t &wp) const
{
	if (mHaveCause) mCause.writeTLV(0x08,dest,wp);
}


void L3ReleaseComplete::parseBody(const L3Frame& src, size_t &rp)
{
	mHaveCause = mCause.parseTLV(0x08,src,rp);
	// ignore the rest
}


void L3ReleaseComplete::text(ostream& os) const
{
	L3CCMessage::text(os);
	if (mHaveCause) os << "cause=(" << mCause << ")";
}




void L3Setup::writeBody( L3Frame &dest, size_t &wp ) const
{
	if (mHaveBearerCapability) mBearerCapability.writeTLV(0x04, dest, wp);
	if (mHaveCallingPartyBCDNumber) mCallingPartyBCDNumber.writeTLV(0x5C,dest, wp);
	if (mHaveCalledPartyBCDNumber) mCalledPartyBCDNumber.writeTLV(0x5E,dest, wp);
}




void L3Setup::parseBody( const L3Frame &src, size_t &rp )
{
	skipTV(0x0D,4,src,rp);		// skip Repeat Indicator.
	skipTLV(0x04,src,rp);		// skip Bearer Capability 1.
	skipTLV(0x04,src,rp);		// skip Bearer Capability 2.
	skipTLV(0x1C,src,rp);		// skip Facility.
	skipTLV(0x1E,src,rp);		// skip Progress.
	skipTLV(0x34,src,rp);		// skip Signal.
	mHaveCallingPartyBCDNumber = mCallingPartyBCDNumber.parseTLV(0x5C,src,rp);
	skipTLV(0x5D,src,rp);		// skip Calling Party Subaddress
	mHaveCalledPartyBCDNumber = mCalledPartyBCDNumber.parseTLV(0x5E,src,rp);
	// ignore the rest
}

size_t L3Setup::l2BodyLength() const 
{
	int len = 0;
	if(mHaveBearerCapability) len += mBearerCapability.lengthTLV();
	if(mHaveCalledPartyBCDNumber) len += mCalledPartyBCDNumber.lengthTLV();
	if(mHaveCallingPartyBCDNumber) len += mCallingPartyBCDNumber.lengthTLV();
	return len;
}


void L3Setup::text(ostream& os) const
{
	L3CCMessage::text(os);
	if(mHaveCallingPartyBCDNumber) 
		os <<"CallingPartyBCDNumber=("<<mCallingPartyBCDNumber<<")";
	if(mHaveCalledPartyBCDNumber) 
		os <<"CalledPartyBCDNumber=("<<mCalledPartyBCDNumber<<")";
}


void L3CCStatus::parseBody( const L3Frame &src, size_t &rp )
{
	mCause.parseLV(src, rp);
	mCallState.parseV(src, rp);
}

void L3CCStatus::writeBody(L3Frame &dest, size_t &wp ) const
{
	mCause.writeLV(dest, wp);
	mCallState.writeV(dest, wp);
}

void L3CCStatus::text(ostream& os) const
{
	L3CCMessage::text(os);
	os << "cause=(" << mCause << ")";
	os << " callState=" << mCallState;	
}



void L3Disconnect::writeBody(L3Frame& dest, size_t &wp) const
{
	mCause.writeLV(dest,wp);
}


void L3Disconnect::parseBody(const L3Frame& src, size_t &rp)
{
	mCause.parseLV(src,rp);
}


void L3Disconnect::text(ostream& os) const
{
	L3CCMessage::text(os);
	os << "cause=(" << mCause << ")";
}




void L3CallConfirmed::parseBody(const L3Frame& src, size_t &rp)
{
	skipTV(0x0D,4,src,rp);		// skip repeat indicator
	skipTLV(0x04,src,rp);		// skip bearer capability 1
	skipTLV(0x04,src,rp);		// skip bearer capability 2
	mHaveCause = mCause.parseTLV(0x08,src,rp);
	// ignore call control capabilities
}


size_t L3CallConfirmed::l2BodyLength() const
{
	size_t sum=0;
	if (mHaveCause) sum += mCause.lengthTLV();
	return sum;
}


void L3CallConfirmed::text(ostream& os) const
{
	L3CCMessage::text(os);
	if (mHaveCause) os << "cause=(" << mCause << ")";
}



void L3StartDTMF::text(ostream& os) const
{
	L3CCMessage::text(os);
	os << "key=" << mKey;
}



void L3StartDTMFAcknowledge::text(ostream& os) const
{
	L3CCMessage::text(os);
	os << "key=" << mKey;
}





void L3StartDTMFReject::text(ostream& os) const
{
	L3CCMessage::text(os);
	os << "cause=(" << mCause << ")";
}





size_t L3Progress::l2BodyLength() const
{
	return mProgress.lengthLV();
}

void L3Progress::writeBody(L3Frame &dest, size_t &wp) const
{
	mProgress.writeLV(dest,wp);
}

void L3Progress::parseBody(const L3Frame& src, size_t &rp)
{
	mProgress.parseLV(src,rp);
	// ignore the rest
}

void L3Progress::text(ostream& os) const
{
	L3CCMessage::text(os);
	os << "prog_ind=(" << mProgress << ")";
}



void L3HoldReject::text(ostream& os) const
{
	L3CCMessage::text(os);
	os << "cause=(" << mCause << ")";
}




// vim: ts=4 sw=4
