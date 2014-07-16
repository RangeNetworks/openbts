/** @file Call Control messags, GSM 04.08 9.3.  */

/*
* Copyright 2008, 2009 Free Software Foundation, Inc.
* Copyright 2011, 2014 Range Networks, Inc.
*
* This software is distributed under multiple licenses;
* see the COPYING file in the main directory for licensing
* information for this specific distribution.
*
* This use of this software may be subject to additional restrictions.
* See the LEGAL file in the main directory for details.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

*/

#define LOG_GROUP LogGroup::GSM		// Can set Log.Level.GSM for debugging





#include <iostream>
#include "GSML3CCMessages.h"
#include <Logger.h>



using namespace std;
using namespace GSM;



ostream& GSM::operator<<(ostream& os, L3CCMessage::MessageType val)
{
	switch (val) {
		case L3CCMessage::Alerting: 
				os << "Alerting"; return os;
		case L3CCMessage::CallConfirmed: 
				os <<"Call Confirmed"; return os;
		case L3CCMessage::CallProceeding: 
				os <<"Call Proceeding"; return os;
		case L3CCMessage::Connect:
				os << "Connect"; return os;
		case L3CCMessage::Disconnect:
				os << "Disconnect"; return os;
		case L3CCMessage::ConnectAcknowledge: 
				os << "Connect Acknowledge"; return os;
		case L3CCMessage::Progress: 
				os << "Progress"; return os;
		case L3CCMessage::Release: 
				os << "Release"; return os;		  
		case L3CCMessage::ReleaseComplete: 
				os << "Release Complete"; return os;		  
		case L3CCMessage::Setup: 
				os << "Setup"; return os;
		case L3CCMessage::EmergencySetup:
				os << "Emergency Setup"; return os;
		case L3CCMessage::CCStatus: 
				os << "Status"; return os;
		case L3CCMessage::StartDTMF:
				os << "Start DTMF"; return os;
		case L3CCMessage::StartDTMFReject:
				os << "Start DTMF Reject"; return os;
		case L3CCMessage::StartDTMFAcknowledge:
				os << "Start DTMF Acknowledge"; return os;
		case L3CCMessage::StopDTMF:
				os << "Stop DTMF"; return os;
		case L3CCMessage::StopDTMFAcknowledge:
				os << "Stop DTMF Acknowledge"; return os;
		case L3CCMessage::Hold:
				os << "Hold"; return os;
		case L3CCMessage::HoldReject:
				os << "Hold Reject"; return os;
	}
	os << hex << "0x" << (int)val << dec;
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


void L3CCCommonIEs::ccCommonText(ostream&os) const
{
	if (mHaveFacility) os << "facility=(" <<mFacility <<")";
	if (mHaveSSVersion) os << "SSVersion=("<<mSSVersion<<")";
}

size_t L3CCCommonIEs::ccCommonLength() const
{
	size_t result = 0;
	if (mHaveFacility) result += mFacility.lengthV();
	if (mHaveSSVersion) result += mSSVersion.lengthV();
	return result;
}

void L3CCCommonIEs::ccCommonParse(const L3Frame& src, size_t &rp)
{
	while (src.size() > rp + 8) {
		unsigned thisIEI = src.peekField(rp,8);
		switch (thisIEI) {
			case 0x1c: mHaveFacility = mFacility.parseTLV(0x1c,src,rp); continue;
			case 0x7f: mHaveSSVersion = mSSVersion.parseTLV(0x7f,src,rp); continue;
			default: return;
		}
	}
}

void L3CCCommonIEs::ccCommonWrite(L3Frame &dest, size_t &wp) const
{
	if (mHaveFacility) mFacility.writeTLV(0x1c,dest,wp);
	if (mHaveSSVersion) mSSVersion.writeTLV(0x7f,dest,wp);
}

size_t L3Alerting::l2BodyLength() const
{
	size_t sum=0;
	if (mHaveProgress) sum += mProgress.lengthTLV();
	sum += ccCommonLength();
	return sum;
}

void L3Alerting::writeBody(L3Frame &dest, size_t &wp) const
{
	if (mHaveProgress) mProgress.writeTLV(0x1E,dest,wp);
	ccCommonWrite(dest,wp);
}

void L3Alerting::parseBody(const L3Frame& src, size_t &rp)
{
	ccCommonParse(src,rp);
	mHaveProgress = mProgress.parseTLV(0x1E,src,rp);
	ccCommonParse(src,rp);
	// ignore the rest
}

void L3Alerting::text(ostream& os) const
{
	L3CCMessage::text(os);
	if (mHaveProgress) os << "progress=(" << mProgress << ")";
	ccCommonText(os);
}


size_t L3CallProceeding::l2BodyLength() const
{
	size_t sum=0;
	if (mHaveProgress) sum += mProgress.lengthTLV();
	if( mBearerCapability.mPresent) sum += mBearerCapability.lengthTLV();
	return sum;
}


void L3CallProceeding::writeBody(L3Frame &dest, size_t &wp) const
{
	if( mBearerCapability.mPresent) mBearerCapability.writeTLV(0x04, dest, wp);
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
	sum += ccCommonLength();
	return sum;
}


void L3Release::writeBody(L3Frame& dest, size_t &wp) const
{
	if (mHaveCause) mCause.writeTLV(0x08,dest,wp);
	ccCommonWrite(dest,wp);
}


void L3Release::parseBody(const L3Frame& src, size_t &rp)
{
	mHaveCause = mCause.parseTLV(0x08,src,rp);
	ccCommonParse(src,rp);
	// ignore the rest
}


void L3Release::text(ostream& os) const
{
	L3CCMessage::text(os);
	if (mHaveCause) os << "cause=(" << mCause << ")";
	ccCommonText(os);
}



size_t L3ReleaseComplete::l2BodyLength() const
{
	size_t sum = 0;
	if (mHaveCause) sum += mCause.lengthTLV();
	sum += ccCommonLength();
	return sum;
}


void L3ReleaseComplete::writeBody(L3Frame& dest, size_t &wp) const
{
	if (mHaveCause) mCause.writeTLV(0x08,dest,wp);
	ccCommonWrite(dest,wp);
}


void L3ReleaseComplete::parseBody(const L3Frame& src, size_t &rp)
{
	mHaveCause = mCause.parseTLV(0x08,src,rp);
	ccCommonParse(src,rp);
	// ignore the rest
}


void L3ReleaseComplete::text(ostream& os) const
{
	L3CCMessage::text(os);
	ccCommonText(os);
	if (mHaveCause) os << "cause=(" << mCause << ")";
}



void L3Setup::writeBody( L3Frame &dest, size_t &wp ) const
{
	if (mBearerCapability.mPresent) mBearerCapability.writeTLV(0x04, dest, wp);
	if (mHaveCallingPartyBCDNumber) mCallingPartyBCDNumber.writeTLV(0x5C,dest, wp);
	if (mHaveCalledPartyBCDNumber) mCalledPartyBCDNumber.writeTLV(0x5E,dest, wp);
	if (mSupportedCodecs.mPresent) mSupportedCodecs.writeTLV(0x40, dest, wp);
	if (mHaveSignal) mSignal.writeTV(0x34,dest,wp);
	ccCommonWrite(dest,wp);
}



// (pat) old doc 4.08, new doc 24.08
// (pat) There are two versions: we are parsing 9.3.23.2, however, the old code had some
// of the IEIs for 0.3.23.1 which is the other direction!  Whatever, I am throwing all the IEIs in here
// so you could parse out the network->mobile message too.
void L3Setup::parseBody( const L3Frame &src, size_t &rp )
{
#if 0 // (pat) 10-2012, Replaced so we do not assume the order of the IEs.
	skipTV(0x0D,4,src,rp);		// skip Repeat Indicator.
	skipTLV(0x04,src,rp);		// skip Bearer Capability 1.
	skipTLV(0x04,src,rp);		// skip Bearer Capability 2.
	skipTLV(0x1C,src,rp);		// skip Facility.
	skipTLV(0x1E,src,rp);		// skip Progress.	(pat) This is an irrelevant error in the original version.
	skipTLV(0x34,src,rp);		// skip Signal.	    (pat) This is an irrelevant error in the original version.
	mHaveCallingPartyBCDNumber = mCallingPartyBCDNumber.parseTLV(0x5C,src,rp);
	skipTLV(0x5D,src,rp);		// skip Calling Party Subaddress
	mHaveCalledPartyBCDNumber = mCalledPartyBCDNumber.parseTLV(0x5E,src,rp);
	// ignore the rest
#else
	while (rp < src.size()) {
		unsigned iei = src.readField(rp,8);
		LOG(DEBUG) << "L3Setup"<<LOGHEX(iei)<<LOGVAR2("len",src.peekField(rp,8))<<LOGVAR(rp)<<LOGVAR2("remaining",src.size()-rp);
		if ((iei & 0xf0) == 0xd0) {continue;}	// Ignore repeat indicators.
		if ((iei & 0xf0) == 0x80) {continue;}	// Ignore priority level.
		switch (iei) {
		case 4:   	// Bearer Capability. (pat) skipped even though there is one in the L3Setup class?
			//mBearerCapability.mPresent = true;
			mBearerCapability.parseLV(src,rp);
			continue;
		case 0x1c:	// facility
			ccCommonParse(src,rp);
			continue;
		case 0x1e:	// Progress indicator. (MTC only)
			skipLV(src,rp);
			continue;
		case 0x34:	// Signal (MTC only)
			mHaveSignal = true;
			mSignal.parseV(src,rp);
			// old: rp++;	// It is TV, two bytes total.
			continue;
		case 0x5c:	// Calling part BCD number.
			// You may encounter this error if the code is accidentally converting between L3Frame and L3Message.
			LOG(ERR) << "Inbound L3Setup contained CallingPartyBCDNumber "<<src;
			mHaveCallingPartyBCDNumber = true;
			mCallingPartyBCDNumber.parseLV(src,rp);
			continue;
		case 0x5d:	// Calling part sub-address
			skipLV(src,rp);
			continue;
		case 0x5e:	// Called party BCD number
			mHaveCalledPartyBCDNumber = true;
			mCalledPartyBCDNumber.parseLV(src,rp);
			continue;
		case 0x6d:	// Called party sub-address
		case 0x74:	// Redirecting party BCD number (MTC only)
		case 0x75:	// Redirecting party sub-address (MTC only)
		case 0x7c:	// lower layer compatibility.
		case 0x7d:	// high layer compatibility.
		case 0x7e:	// user-user.
			skipLV(src,rp);
			continue;
		case 0x7f:	// SS version (MOC only)
			ccCommonParse(src,rp);
			continue;
		case 0xa1:	// CLIR suppression (MOC only)
		case 0xa2:	// CLIR invocation (MOC only)
			continue;	// They are type T, single byte.
		case 0x15:	// CC capabilities (MOC only)
		case 0x1d:	// Facility advanced recall assignment (MOC only)
		case 0x1b:	// Facility recall alignment not essential (MOC only)
		case 0x2d:	// Stream Identifier. (MOC only)
		case 0x2e:	// (pat) Emergency category 24.008 10.5.4.33.  Not really applicable to us - it will be "manually initiated".
		case 0x19:	// Alert (MTC only)
		case 0x2f:	// Network Call Control Capabilities (MTC only)
		case 0x3a:	// Cause of No CLI (MTC only)
		case 0x41:	// Backup bearer capability (MTC only)
			skipLV(src,rp);
			continue;
		case 0x40:	// Supported Codecs (MOC only)
			//mSupportedCodecs.mPresent = true;
			mSupportedCodecs.parseLV(src,rp);
			continue;
		case 0xa3:	// redial (MOC only)
			continue;	// Type T, single byte.
		default:
			// Hmmm.
			LOG(WARNING) << "Unexpected"<<LOGVAR(iei)<<" in L3Setup message";
			// Assume TLV, and if rp overflows, the while loop will terminate, although rp is trashed.
			skipLV(src,rp);
			continue;
		}
	}
#endif
}

size_t L3Setup::l2BodyLength() const 
{
	int len = 0;
	if(mBearerCapability.mPresent) len += mBearerCapability.lengthTLV();
	if(mHaveCalledPartyBCDNumber) len += mCalledPartyBCDNumber.lengthTLV();
	if(mHaveCallingPartyBCDNumber) len += mCallingPartyBCDNumber.lengthTLV();
	if (mHaveSignal) len += mSignal.lengthTV();
	len += ccCommonLength();
	return len;
}


void L3Setup::text(ostream& os) const
{
	L3CCMessage::text(os);
	if(mHaveCallingPartyBCDNumber) 
		os <<" CallingPartyBCDNumber=("<<mCallingPartyBCDNumber<<")";
	if(mHaveCalledPartyBCDNumber) 
		os <<" CalledPartyBCDNumber=("<<mCalledPartyBCDNumber<<")";
	if (mBearerCapability.mPresent)
		os <<" BearerCapability=("<<mBearerCapability<<")";
	if (mSupportedCodecs.mPresent) 
		os <<" SupportedCodecList=("<<mSupportedCodecs<<")";
	if (mHaveSignal) { os << " "; mSignal.text(os); }
	os << " CodecSet="<<getCodecSet();
	ccCommonText(os);
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
#if 0 // (pat) 10-2012, Replaced so we do not assume the order of the IEs.
	skipTV(0x0D,4,src,rp);		// skip repeat indicator
	skipTLV(0x04,src,rp);		// skip bearer capability 1
	skipTLV(0x04,src,rp);		// skip bearer capability 2
	mHaveCause = mCause.parseTLV(0x08,src,rp);
	skipTLV(0x15,src,rp);		// Skip CC capabilities
	skipTLV(0x2D,src,rp);		// Skip CC capabilities
	// ignore call control capabilities
#else
	while (rp < src.size()) {
		unsigned iei = src.readField(rp,8);
		LOG(DEBUG) << "L3Setup"<<LOGHEX(iei)<<LOGVAR2("len",src.peekField(rp,8))<<LOGVAR(rp)<<LOGVAR2("remaining",src.size()-rp);
		if ((iei & 0xf0) == 0xd0) {continue;}	// Ignore repeat indicator.
		switch (iei) {
		case 8:	// cause
			mHaveCause = true;
			mCause.parseLV(src,rp);
			continue;
		case 0x40:	// supported codec list.
			//mSupportedCodecs.mPresent = true;
			mSupportedCodecs.parseLV(src,rp);
			continue;
		case 4:		// bearer capability
			//mBearerCapability.mPresent = true;
			mBearerCapability.parseLV(src,rp);
			continue;
		case 0x15: // CC Capabilities
		case 0x2d: // stream identifier.
			// All TLV.
			skipLV(src,rp);
			continue;
		default:
			// Hmmm.
			LOG(WARNING) << "Unexpected"<<LOGVAR(iei)<<" in CallConfirmed message";
			// Assume TLV, and if rp overflows, the while loop will terminate, although rp is trashed.
			skipLV(src,rp);
			continue;
		}
	}
#endif
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
	if (mHaveCause) os << " cause=(" << mCause << ")";
	if (mBearerCapability.mPresent) os <<" BearerCapability=("<<mBearerCapability<<")";
	if (mSupportedCodecs.mPresent) os <<" SupportedCodecList=("<<mSupportedCodecs<<")";
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
