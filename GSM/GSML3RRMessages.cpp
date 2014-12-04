/**@file
  @brief GSM Radio Resorce messages, from GSM 04.08 9.1.
*/
/*
* Copyright 2008, 2009, 2010 Free Software Foundation, Inc.
* Copyright 2011, 2012, 2014 Range Networks, Inc.
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




#include <typeinfo>
#include <iostream>

#include "GSML3RRMessages.h"
#include "GSMConfig.h"	// For gBTS
#include <Logger.h>


using namespace std;
using namespace GSM;





void L3Message::writeBody(L3Frame&,size_t&) const
{
	LOG(ERR) << "not implemented for " << MTI();
	assert(0);
}

void L3Message::parseBody(const L3Frame&, size_t&)
{
	LOG(ERR) << "not implemented for " << MTI();
	assert(0);
}


const char *L3RRMessage::name(MessageType mt)
{
	switch (mt) {
		case L3RRMessage::SystemInformationType1: 
			return "System Information Type 1"; 
		case L3RRMessage::SystemInformationType2: 
			return "System Information Type 2"; 
		case L3RRMessage::SystemInformationType2bis: 
			return "System Information Type 2bis"; 
		case L3RRMessage::SystemInformationType2ter: 
			return "System Information Type 2ter"; 
		case L3RRMessage::SystemInformationType3: 
			return "System Information Type 3"; 
		case L3RRMessage::SystemInformationType4: 
			return "System Information Type 4"; 
		case L3RRMessage::SystemInformationType5: 
			return "System Information Type 5"; 
		case L3RRMessage::SystemInformationType5bis: 
			return "System Information Type 5bis"; 
		case L3RRMessage::SystemInformationType5ter: 
			return "System Information Type 5ter"; 
		case L3RRMessage::SystemInformationType6: 
			return "System Information Type 6"; 
		case L3RRMessage::SystemInformationType7: 
			return "System Information Type 7"; 
		case L3RRMessage::SystemInformationType8: 
			return "System Information Type 8"; 
		case L3RRMessage::SystemInformationType9: 
			return "System Information Type 9"; 
		case L3RRMessage::SystemInformationType13: 
			return "System Information Type 13"; 
		case L3RRMessage::SystemInformationType16: 
			return "System Information Type 16"; 
		case L3RRMessage::SystemInformationType17: 
			return "System Information Type 17"; 
		case L3RRMessage::PagingResponse: 
			return "Paging Response"; 
		case L3RRMessage::PagingRequestType1: 
			return "Paging Request Type 1"; 
		case L3RRMessage::MeasurementReport: 
			return "Measurement Report"; 
		case L3RRMessage::AssignmentComplete: 
			return "Assignment Complete"; 
		case L3RRMessage::ImmediateAssignment: 
			return "Immediate Assignment"; 
		case L3RRMessage::ImmediateAssignmentReject: 
			return "Immediate Assignment Reject"; 
		case L3RRMessage::AssignmentCommand: 
			return "Assignment Command"; 
		case L3RRMessage::AssignmentFailure: 
			return "Assignment Failure"; 
		case L3RRMessage::ChannelRelease: 
			return "Channel Release"; 
		case L3RRMessage::ChannelModeModify:
			return "Channel Mode Modify"; 
		case L3RRMessage::ChannelModeModifyAcknowledge:
			return "Channel Mode Modify Acknowledge"; 
		case L3RRMessage::GPRSSuspensionRequest: 
			return "GPRS Suspension Request"; 
		case L3RRMessage::ClassmarkEnquiry: 
			return "Classmark Enquiry"; 
		case L3RRMessage::ClassmarkChange: 
			return "Classmark Change"; 
		case L3RRMessage::RRStatus:
			return "RR Status"; 
		case L3RRMessage::ApplicationInformation:
			return "Application Information"; 
		case L3RRMessage::HandoverCommand:
			return "Handover Command"; 
		case L3RRMessage::HandoverComplete:
			return "Handover Complete"; 
		case L3RRMessage::HandoverFailure:
			return "Handover Failure"; 
		case L3RRMessage::CipheringModeCommand:
			return "Ciphering Mode Command";
		case L3RRMessage::CipheringModeComplete:
			return "Ciphering Mode Complete";
		case L3RRMessage::PhysicalInformation:
			return "Physical Information"; 
		default:
			return "?";
	}
}

ostream& GSM::operator<<(ostream& os, L3RRMessage::MessageType val)
{
	const char *result = L3RRMessage::name(val);
	if (result[0] == '?') {
		os << hex << "0x" << (int)val << dec;
	} else {
		os << result;
	}
	return os;
}


void L3RRMessage::text(ostream& os) const
{
	os << "RR " << (MessageType) MTI() << " ";
}


L3RRMessage* GSM::L3RRFactory(L3RRMessage::MessageType MTI)
{
	switch (MTI) {
		case L3RRMessage::ChannelRelease: return new L3ChannelRelease();
		case L3RRMessage::AssignmentComplete: return new L3AssignmentComplete();
		case L3RRMessage::AssignmentFailure: return new L3AssignmentFailure();
		case L3RRMessage::RRStatus: return new L3RRStatus();
		case L3RRMessage::PagingResponse: return new L3PagingResponse();
		case L3RRMessage::ChannelModeModifyAcknowledge: return new L3ChannelModeModifyAcknowledge();
		case L3RRMessage::ClassmarkChange: return new L3ClassmarkChange();
		case L3RRMessage::ClassmarkEnquiry: return new L3ClassmarkEnquiry();
		case L3RRMessage::MeasurementReport: return new L3MeasurementReport();
		case L3RRMessage::ApplicationInformation: return new L3ApplicationInformation();
		case L3RRMessage::HandoverComplete: return new L3HandoverComplete();
		case L3RRMessage::HandoverFailure: return new L3HandoverFailure();
		case L3RRMessage::CipheringModeComplete: return new L3CipheringModeComplete();
		case L3RRMessage::HandoverCommand: return new L3HandoverCommand();
        // Partial support just to get along with some phones.
        case L3RRMessage::GPRSSuspensionRequest: return new L3GPRSSuspensionRequest();
		default:
			LOG(WARNING) << "no L3 RR factory support for " << MTI;
			return NULL;
	}
}

L3RRMessage* GSM::parseL3RR(const L3Frame& source)
{
	L3RRMessage::MessageType MTI = (L3RRMessage::MessageType)source.MTI();
	LOG(DEBUG) << "parseL3RR MTI="<<MTI;

	L3RRMessage *retVal = L3RRFactory(MTI);
	if (retVal==NULL) return NULL;

	retVal->parse(source);
	return retVal;
}



/**
This is a local function to map the GSM::ChannelType enum
to one of the codes from GMS 04.08 10.5.2.8.
*/
unsigned channelNeededCode(ChannelType wType)
{
	switch (wType) {
		case AnyDCCHType: return 0;
		case SDCCHType: return 1;
		case TCHFType: return 2;
		case AnyTCHType: return 3;
		default: assert(0);
	}
}



size_t L3PagingRequestType1::l2BodyLength() const
{
	int sz = mMobileIDs.size();
	assert(sz<=2);
	size_t sum=1;
	sum += mMobileIDs[0].lengthLV();
	if (sz>1) sum += mMobileIDs[1].lengthTLV();
	return sum;
}



void L3PagingRequestType1::writeBody(L3Frame& dest, size_t &wp) const
{
	// See GSM 04.08 9.1.22.
	// Page Mode Page Mode M V 1/2 10.5.2.26    
	// Channels Needed  M V 1/2 
	// Mobile Identity 1 M LV 2-9 10.5.1.4    
	// 0x17 Mobile Identity 2 O TLV  3-10 10.5.1.4    

	int sz = mMobileIDs.size();
	assert(sz<=2);
	// Remember to reverse orders of 1/2-octet fields.
	// Because GSM transmits LSB-first within each byte.
	// (pat) No, it is because the fields are written MSB first here,
	// and then later byte-reversed in the encoder before being sent to radio LSB first.
	dest.writeField(wp,channelNeededCode(mChannelsNeeded[1]),2);
	dest.writeField(wp,channelNeededCode(mChannelsNeeded[0]),2);
	// "normal paging", GSM 04.08 Table 10.5.63
	dest.writeField(wp,0x0,4);
	// the actual mobile IDs
	mMobileIDs[0].writeLV(dest,wp);
	if (sz>1) mMobileIDs[1].writeTLV(0x17,dest,wp);
}


void L3PagingRequestType1::text(ostream& os) const
{
	L3RRMessage::text(os);
	os << " mobileIDs=(";
	for (unsigned i=0; i<mMobileIDs.size(); i++) {
		os << "(" << mMobileIDs[i] << "," << mChannelsNeeded[i] << ") ";
	}
	os << ")";
}


size_t L3PagingResponse::l2BodyLength() const
{
	return 1 + mClassmark.lengthLV() + mMobileID.lengthLV();
}

void L3PagingResponse::parseBody(const L3Frame& src, size_t &rp)
{
	// THIS CODE IS CORRECT.  DON'T CHANGE IT. -- DAB
	rp += 8;			// skip cipher key seq # and spare half octet
	// TREAT THIS AS LV!!
	mClassmark.parseLV(src,rp);
	// We only care about the mobile ID.
	mMobileID.parseLV(src,rp);
}

void L3PagingResponse::text(ostream& os) const
{
	L3RRMessage::text(os);
	os << "mobileID=(" << mMobileID << ")";
	os << " classmark=(" << mClassmark << ")";
}




void L3SystemInformationType1::writeBody(L3Frame& dest, size_t &wp) const
{
/*
	System Information Message Type 1, GSM 04.08 9.1.31
	- Cell Channel Description 10.5.2.1b M V 16 
	- RACH Control Parameters 10.5.2.29 M V 3 
*/
	mCellChannelDescription.writeV(dest,wp);
	mRACHControlParameters.writeV(dest,wp);
	// (pat 4-2014) SysInfo Type 1 Rest Octets are "mandatory" as per 44.018 9.1.31
	// It is one byte.  We are sending the default padding for this byte (0x2b), which is fine;
	// per 10.5.2.32 the "L" (default) value indicates that NCH is not present.
}


void L3SystemInformationType1::text(ostream& os) const
{
	L3RRMessage::text(os);
	os << "cellChannelDescription=(" << mCellChannelDescription << ")";
	os << " RACHControlParameters=(" << mRACHControlParameters << ")";
}



void L3SystemInformationType2::writeBody(L3Frame& dest, size_t &wp) const
{
/*
	System Information Type 2, GSM 04.08 9.1.32.
	- BCCH Frequency List 10.5.2.22 M V 16 
	- NCC Permitted 10.5.2.27 M V 1 
	- RACH Control Parameter 10.5.2.29 M V 3 
*/
	mBCCHFrequencyList.writeV(dest,wp);
	mNCCPermitted.writeV(dest,wp);
	mRACHControlParameters.writeV(dest,wp);
}


void L3SystemInformationType2::text(ostream& os) const
{
	L3RRMessage::text(os);
	os << "BCCHFrequencyList=(" << mBCCHFrequencyList << ")";
	os << " NCCPermitted=(" << mNCCPermitted << ")";
	os << " RACHControlParameters=(" << mRACHControlParameters << ")";
}




void L3SystemInformationType3::writeBody(L3Frame& dest, size_t &wp) const
{
/*
	System Information Type 3, GSM 04.08 9.1.35
	- Cell Identity 10.5.1.1 M V 2 
	- Location Area Identification 10.5.1.3 M V 5 
	- Control Channel Description 10.5.2.11 M V 3 
	- Cell Options (BCCH) 10.5.2.3 M V 1 
	- Cell Selection Parameters 10.5.2.4 M V 2 
	- RACH Control Parameters 10.5.2.29 M V 3 
	- Rest Octets 10.5.2.34 O CSN.1
*/
	size_t wpstart = wp;
	LOG(DEBUG) << dest;
	mCI.writeV(dest,wp);
	LOG(DEBUG) << dest;
	mLAI.writeV(dest,wp);
	LOG(DEBUG) << dest;
	mControlChannelDescription.writeV(dest,wp);
	LOG(DEBUG) << dest;
	mCellOptions.writeV(dest,wp);
	LOG(DEBUG) << dest;
	mCellSelectionParameters.writeV(dest,wp);
	LOG(DEBUG) << dest;
	mRACHControlParameters.writeV(dest,wp);
	LOG(DEBUG) << dest;
	/*if (mHaveRestOctets)*/ mRestOctets.writeV(dest,wp);
	LOG(DEBUG) << dest;
	assert(wp-wpstart == fullBodyLength() * 8);
}


void L3SystemInformationType3::text(ostream& os) const
{
	L3RRMessage::text(os);
	os << "LAI=(" << mLAI << ")";
	os << " CI=" << mCI;
	os << " controlChannelDescription=(" << mControlChannelDescription << ")";
	os << " cellOptions=(" << mCellOptions << ")";
	os << " cellSelectionParameters=(" << mCellSelectionParameters << ")";
	os << " RACHControlParameters=(" << mRACHControlParameters << ")";
	/*if (mHaveRestOctets)*/ os << " SI3RO=(" << mRestOctets << ")";
}

L3SIType4RestOctets::L3SIType4RestOctets()
{
#if GPRS_PAT|GPRS_TESTSI4
	mRA_COLOUR = gConfig.getNum("GPRS.RA_COLOUR");
#endif
}

// GSM04.08 sec 10.5.2.35
void L3SIType4RestOctets::writeV(L3Frame &dest, size_t &wp) const
{
#if GPRS_PAT|GPRS_TESTSI4
	dest.writeL(wp); // SI4 Rest Octets_O -> Optional selection parameters
	dest.writeL(wp); // SI4 Rest Octets_O -> Optional Power offset
	if (GPRS::GPRSConfig::IsEnabled()) {
		dest.writeH(wp); // SI4 Rest Octets_O -> GPRS Indicator (present)
		dest.writeField(wp,mRA_COLOUR,3);
		dest.write0(wp);	// SI13 message is sent on BCCH Norm schedule.
	} else {
		dest.writeL(wp); // SI4 Rest Octets_O -> GPRS Indicator (absent)
	}
	dest.writeL(wp);	// Indicates 'Break Indicator' branch of message.
	dest.writeL(wp);	// Break Indicator == L means no extra info sent in SI Type 7 and 8.
#endif
}

size_t L3SIType4RestOctets::lengthBits() const
{
#if GPRS_PAT|GPRS_TESTSI4
	return 1 + 1 + (GPRS::GPRSConfig::IsEnabled() ? 5 : 1) + 1 + 1;
#else
	return 0;
#endif
}

void L3SIType4RestOctets::writeText(std::ostream& os) const
{
#if GPRS_PAT|GPRS_TESTSI4
	if (GPRS::GPRSConfig::IsEnabled()) {
		os << "GPRS enabled; RA_COLOUR=(" << mRA_COLOUR << ")";
	}
#endif
}



L3SystemInformationType4::L3SystemInformationType4()
	:L3RRMessageRO(),
	mHaveCBCH(false)
{
	// Dont advertise CBCH unless and until we have a valid channel description for it.
	if (isCBSEnabled() && gBTS.mCBCHDescription.initialized()) {
		mHaveCBCH = true;
		mCBCHChannelDescription = gBTS.mCBCHDescription;
	}
}


size_t L3SystemInformationType4::l2BodyLength() const
{
	size_t len = mLAI.lengthV();
	len += mCellSelectionParameters.lengthV();
	len += mRACHControlParameters.lengthV();
	if (mHaveCBCH) len += mCBCHChannelDescription.lengthTV();
	return len;
}

size_t L3SystemInformationType4::restOctetsLength() const
{
	return mType4RestOctets.lengthV();
}


void L3SystemInformationType4::writeBody(L3Frame& dest, size_t &wp) const
{
/*
	System Information Type 4, GSM 04.08 9.1.36
	- Location Area Identification 10.5.1.3 M V 5 
	- Cell Selection Parameters 10.5.2.4 M V 2 
	- RACH Control Parameters 10.5.2.29 M V 3 
*/
	size_t wpstart = wp;
	mLAI.writeV(dest,wp);
	mCellSelectionParameters.writeV(dest,wp);
	mRACHControlParameters.writeV(dest,wp);
	if (mHaveCBCH) {
		mCBCHChannelDescription.writeTV(0x64,dest,wp);
	}
	mType4RestOctets.writeV(dest,wp);
	while (wp & 7) { dest.writeL(wp); } // Zero to byte boundary.
	assert(wp-wpstart == fullBodyLength() * 8);
}


void L3SystemInformationType4::text(ostream& os) const
{
	L3RRMessage::text(os);
	os << "LAI=(" << mLAI << ")";
	os << " cellSelectionParameters=(" << mCellSelectionParameters << ")";
	os << " RACHControlParameters=(" << mRACHControlParameters << ")";
	if (mHaveCBCH) {
		os << "CBCHChannelDescription=(" << mCBCHChannelDescription << ")";
	}
	mType4RestOctets.writeText(os);
}



void L3SystemInformationType5::writeBody(L3Frame& dest, size_t &wp) const
{
/*
	System Information Type 5, GSM 04.08 9.1.37
	- BCCH Frequency List 10.5.2.22 M V 16 
*/
	mBCCHFrequencyList.writeV(dest,wp);
	wp -= 111;
	int p = gConfig.getFloat("GSM.Cipher.RandomNeighbor") * (float)0xFFFFFF;
	for (unsigned i = 1; i <= 111; i++) {
		int b = ((random() & 0xFFFFFF) < p) | dest.peekField(wp, 1);
		dest.writeField(wp, b, 1);
	}
}


void L3SystemInformationType5::text(ostream& os) const
{
	L3RRMessage::text(os);
	os << "BCCHFrequencyList=(" << mBCCHFrequencyList << ")";
}




void L3SystemInformationType6::writeBody(L3Frame& dest, size_t &wp) const
{
/*
	System Information Type 6, GSM 04.08 9.1.40
	- Cell Identity 10.5.1.11 M V 2 
	- Location Area Identification 10.5.1.3 M V 5 
	- Cell Options (SACCH) 10.5.2.3 M V 1 
	- NCC Permitted 10.5.2.27 M V 1 
*/
	mCI.writeV(dest,wp);
	mLAI.writeV(dest,wp);
	mCellOptions.writeV(dest,wp);
	mNCCPermitted.writeV(dest,wp);
}


void L3SystemInformationType6::text(ostream& os) const
{
	L3RRMessage::text(os);
	os << "CI=" << mCI;
	os << " LAI=(" << mLAI << ")";
	os << " cellOptions=(" << mCellOptions << ")";
	os << " NCCPermitted=(" << mNCCPermitted << ")";
}

void L3ImmediateAssignment::writeStartTime(L3Frame& dest, size_t &wp) const
{
	// The names T1, T2, T3 are defined in GSM 4.08 table 10.5.2.39 (same as 10.5.79)
	unsigned T1 = (mStartTimeFrame/1326)%32;
	unsigned T3 = mStartTimeFrame%51;
	unsigned T2 = mStartTimeFrame%26;

	// Recompute original startframe:
	// Note that T3-T2 may be negative:
	//int recomputed = 51 * (((int)T3-(int)T2) % 26) + T3 + 51 * 26 * T1;
	//unsigned startframemod = (startframe % 42432);

	// Note: The fields are written here Most-Significant-Bit first in each byte,
	// then the bytes are reversed in the encoder before being sent to the radio.
	// 		 	7      6      5      4      3     2      1      0
	//	  [			T1[4:0]                 ][   T3[5:3]        ]  Octet 1
	//	  [       T3[2:0]     ][            T2[4:0]             ]  Octet 2
	dest.writeField(wp,T1,5);
	dest.writeField(wp,T3,6);	// Yes T3 comes before T2.
	dest.writeField(wp,T2,5);
}

void L3ImmediateAssignment::writeBody( L3Frame &dest, size_t &wp ) const
{
	size_t wpstart = wp;
	/*
	- Page Mode 10.5.2.26 M V 1/2 
	- Dedicated mode or TBF 10.5.2.25b M V 1/2 
	- Channel Description 10.5.2.5 C V 3 
	- Request Reference 10.5.2.30 M V 3 
	- Timing Advance 10.5.2.40 M V 1 
	(ignoring optional elements)
	*/
	// reverse order of 1/2-octet fields
	// (pat) Because we are writing MSB first here.
	mDedicatedModeOrTBF.writeV(dest, wp);
	mPageMode.writeV(dest, wp);
	mChannelDescription.writeV(dest, wp);	// From L3ChannelDescription
	mRequestReference.writeV(dest, wp);
	mTimingAdvance.writeV(dest, wp);
	// No mobile allocation in non-hopping systems.
	// A zero-length LV.  Just write L=0.  (pat) LV, etc. defined in GSM04.07 sec 11.2.1.1
	dest.writeField(wp,0,8);
	if (mStartTimePresent) {
		dest.writeField(wp,0x7c,8);	// The type of the TLV.
		writeStartTime(dest,wp);
	}
	mIARestOctets.writeBits(dest,wp);
	assert(wp-wpstart == fullBodyLength() * 8);
}


void L3ImmediateAssignment::text(ostream& os) const
{
	L3RRMessage::text(os);
	os << "PageMode=("<<mPageMode<<")";
	os << " DedicatedModeOrTBF=("<<mDedicatedModeOrTBF<<")";
	os << " ChannelDescription=("<<mChannelDescription<<")";
	os << " RequestReference=("<<mRequestReference<<")";
	os << " TimingAdvance="<<mTimingAdvance;
	if (mStartTimePresent) {
		Time now = gBTS.time();
		int msecsFuture = ((Time(mStartTimeFrame) - now.FN()).FN() * gFrameMicroseconds) / 1000;
		os <<LOGVARM(mStartTimeFrame) <<"(" <<msecsFuture <<"ms)";
	}
	mIARestOctets.text(os); 
}


void L3ChannelRequest::text(ostream& os) const
{
	L3RRMessage::text(os);
	os << "RA=" << mRA;
	os << " time=" << mTime;
}


void L3ChannelRelease::writeBody( L3Frame &dest, size_t &wp ) const 
{
	mRRCause.writeV(dest, wp);
}

void L3ChannelRelease::text(ostream& os) const
{
	L3RRMessage::text(os);
	os <<"cause="<< mRRCause;
}



void L3AssignmentCommand::writeBody( L3Frame &dest, size_t &wp ) const
{
	mChannelDescription.writeV(dest, wp);
	mPowerCommand.writeV(dest, wp);
	if (mHaveMode1) mMode1.writeTV(0x63,dest,wp); 
	if (isAMR()) mMultiRate.writeTLV(3,dest,wp);
}

size_t L3AssignmentCommand::l2BodyLength() const 
{
	size_t len = mChannelDescription.lengthV();
	len += mPowerCommand.lengthV();
	if (mHaveMode1) len += mMode1.lengthTV();
	if (isAMR()) len += mMultiRate.lengthTLV();
	return len;
}


void L3AssignmentCommand::text(ostream& os) const
{
	L3RRMessage::text(os);
	os <<"channelDescription=("<<mChannelDescription<<")";
	os <<" powerCommand="<<mPowerCommand;
	if (mHaveMode1) os << " mode1=" << mMode1;
	if (isAMR()) os << " multirate=" << mMultiRate;
}


void L3AssignmentComplete::parseBody(const L3Frame& src, size_t &rp)
{
	mCause.parseV(src,rp);
}


void L3AssignmentComplete::text(ostream& os) const
{
	L3RRMessage::text(os);
	os << "cause=" << mCause;
}


void L3AssignmentFailure::parseBody(const L3Frame& src, size_t &rp)
{
	mCause.parseV(src,rp);
}

void L3AssignmentFailure::text(ostream& os) const
{
	L3RRMessage::text(os);
	os << "cause=" << mCause;
}



void L3RRStatus::parseBody(const L3Frame& src, size_t &rp)
{
	mCause.parseV(src,rp);
}

void L3RRStatus::text(ostream& os) const
{
	L3RRMessage::text(os);
	os << "cause=" << mCause;
}



void L3ImmediateAssignmentReject::writeBody(L3Frame& dest, size_t &wp) const
{
	unsigned count = mRequestReference.size();
	assert(count<=4);
	dest.writeField(wp,0,4);		// spare 1/2 octet
	mPageMode.writeV(dest,wp);
	for (unsigned i=0; i<count; i++) {
		mRequestReference[i].writeV(dest,wp);
		mWaitIndication.writeV(dest,wp);
	}
	unsigned fillCount = 4-count;
	for (unsigned i=0; i<fillCount; i++) {
		mRequestReference[count-1].writeV(dest,wp);
		mWaitIndication.writeV(dest,wp);
	}
}

void L3ImmediateAssignmentReject::text(ostream& os) const
{
	L3RRMessage::text(os);
	os << "pageMode=" << mPageMode;
	os << " T3122=" << mWaitIndication;
	os << " requestReferences=(";
	for (unsigned i=0; i<mRequestReference.size(); i++) {
		os << mRequestReference[i] << ", ";
	}
	os << ")";
}



void L3ChannelModeModify::writeBody(L3Frame &dest, size_t& wp) const
{
	mDescription.writeV(dest,wp);
	mMode.writeV(dest,wp);
	if (isAMR()) {
		mMultiRate.writeTLV(3,dest,wp); // 3 == Multi-Rate configurion IEI
	}
}


void L3ChannelModeModify::text(ostream& os) const
{
	L3RRMessage::text(os);
	os << "description=(" << mDescription << ")";
	os << " mode=(" << mMode << ")";
	if (isAMR()) {
		os << " multirate=(" << mMultiRate << ")";
	}
}


void L3ChannelModeModifyAcknowledge::parseBody(const L3Frame &src, size_t& rp)
{
	mDescription.parseV(src,rp);
	mMode.parseV(src,rp);
}


void L3ChannelModeModifyAcknowledge::text(ostream& os) const
{
	L3RRMessage::text(os);
	os << "description=(" << mDescription << ")";
	os << " mode=(" << mMode << ")";
}

void L3MeasurementReport::parseBody(const L3Frame& frame, size_t &rp)
{
	mResults.parseV(frame,rp);
}

void L3MeasurementReport::text(ostream& os) const
{
	L3RRMessage::text(os);
	os << mResults;
}


// L3ApplicationInformation

L3ApplicationInformation::~L3ApplicationInformation()
{
}

L3ApplicationInformation::L3ApplicationInformation()
{
}

L3ApplicationInformation::
    L3ApplicationInformation(BitVector2& data, unsigned protocolIdentifier,
                             unsigned cr, unsigned firstSegment, unsigned lastSegment)
        : L3RRMessageNRO(), mID(protocolIdentifier)
        , mFlags(cr, firstSegment, lastSegment)
        , mData(data)
{
}

void L3ApplicationInformation::writeBody( L3Frame &dest, size_t &wp ) const
{
/*
- APDU ID 10.5.2.48 M V 1/2
- APDU Flags 10.5.2.49 M V 1/2
- APDU Data 10.5.2.50 M LV N
*/
	// reverse order of 1/2-octet fields
	// (pat) Because we are writing MSB first here.
	static size_t start = wp;
	LOG(DEBUG) << "L3ApplicationInformation: written " << wp - start << " bits";
	mFlags.writeV(dest, wp);
	LOG(DEBUG) << "L3ApplicationInformation: written " << wp - start << " bits";
	mID.writeV(dest, wp);
	LOG(DEBUG) << "L3ApplicationInformation: written " << wp - start << " bits";
	mData.writeLV(dest, wp);
	LOG(DEBUG) << "L3ApplicationInformation: written " << wp - start << " bits";
}


void L3ApplicationInformation::text(ostream& os) const
{
	L3RRMessage::text(os);
	os << "ID=("<<mID<<")";
	os << " Flags=("<<mFlags<<")";
	os << " Data=("<<mData<<")";
}

void L3ApplicationInformation::parseBody(const L3Frame& src, size_t &rp)
{
	// reverse order of 1/2-octet fields
	// (pat) Because we are writing MSB first here.
	mFlags.parseV(src, rp);
	mID.parseV(src, rp);
	mData.parseLV(src, rp);
}

size_t L3ApplicationInformation::l2BodyLength() const
{
	return 1 + mData.lengthLV();
}


// 3GPP 44.018 9.1.13b
// (pat 3-2012) Added parsing.
void L3GPRSSuspensionRequest::parseBody(const L3Frame &src, size_t& rp)
{
	// The TLLI is what we most need out of this message to identify the MS.
	// Note that TLLI is not just a simple number; encoding defined in 23.003.
	// We dont worry about it here; the SGSN handles that.
	mTLLI = src.readField(rp,4*8);
	// 3GPP 24.008 10.5.5.15 Routing Area Identification.
	// Similar to L3LocationAreaIdentity but includes RAC too.
	// We dont really care about it now, and when we do, all we will care
	// is if it matches our own or not.
	// Just squirrel away the 6 bytes as a ByteVector.
	// This is an immediate object whose memory will be deleted automatically.
	mRaId = ByteVector(src.alias().segment(rp,6*8));
	rp += 6*8;	// And skip over it.
	mSuspensionCause = src.readField(rp,1*8);	// 10.5.2.47
	// Optional service support, IEI=0x01.
	// It is for MBMS and we dont really care about it, but get it anyway.
	if (rp>=src.size()+2*8 && 0x01 == src.peekField(rp,8)) {
		rp+=8;	// Skip over the IEI type
		mServiceSupport = src.readField(rp,8);
	}
}
void L3GPRSSuspensionRequest::text(ostream& os) const
{
	L3RRMessage::text(os);
	os <<LOGVAR(mTLLI)<<LOGVAR(mRaId)<<LOGVAR(mSuspensionCause)<<LOGVAR(mServiceSupport);
}


void L3ClassmarkChange::parseBody(const L3Frame &src, size_t &rp)
{
	mClassmark.parseLV(src,rp);
	mHaveAdditionalClassmark = mAdditionalClassmark.parseTLV(0x20,src,rp);
}

size_t L3ClassmarkChange::l2BodyLength() const
{
	size_t sum = mClassmark.lengthLV();
	if (mHaveAdditionalClassmark) sum += mAdditionalClassmark.lengthTLV();
	return sum;
}

void L3ClassmarkChange::text(ostream& os) const
{
	L3RRMessage::text(os);
	os << "classmark=(" << mClassmark << ")";
	if (mHaveAdditionalClassmark)
		os << " +classmark=(" << mAdditionalClassmark << ")";
}


size_t L3HandoverCommand::l2BodyLength() const
{
	size_t sum =
		mCellDescription.lengthV() +
		mChannelDescriptionAfter.lengthV() +
		mHandoverReference.lengthV() +
		mPowerCommandAccessType.lengthV() +
		mSynchronizationIndication.lengthV();
	return sum;
}

void L3HandoverCommand::writeBody(L3Frame& frame, size_t& wp) const
{
	mCellDescription.writeV(frame,wp);
	mChannelDescriptionAfter.writeV(frame,wp);
	mHandoverReference.writeV(frame,wp);
	mPowerCommandAccessType.writeV(frame,wp);
	mSynchronizationIndication.writeV(frame,wp);
}

void L3HandoverCommand::parseBody(const L3Frame& frame, size_t& rp)
{
	mCellDescription.parseV(frame,rp);
	mChannelDescriptionAfter.parseV(frame,rp);
	mHandoverReference.parseV(frame,rp);
	mPowerCommandAccessType.parseV(frame,rp);
	mSynchronizationIndication.parseV(frame,rp);
}

void L3HandoverCommand::text(ostream& os) const
{
	L3RRMessage::text(os);
	os << "cell=(" << mCellDescription << ")";
	os << " channelr=(" << mChannelDescriptionAfter << ")";
	os << " ref=" << mHandoverReference;
	os << " powerAndAccess=(" << mPowerCommandAccessType << ")";
	os << " synchronization=(" << mSynchronizationIndication << ")";
}


void L3HandoverComplete::parseBody(const L3Frame& frame, size_t& rp)
{
	mCause.parseV(frame,rp);
}

void L3HandoverComplete::text(ostream& os) const
{
	L3RRMessage::text(os);
	os << "cause=" << mCause;
}

void L3HandoverFailure::parseBody(const L3Frame& frame, size_t& rp)
{
	mCause.parseV(frame,rp);
}

void L3HandoverFailure::text(ostream& os) const
{
	L3RRMessage::text(os);
	os << "cause=" << mCause;
}

int L3CipheringModeCommand::MTI() const
{
	return CipheringModeCommand;
}

void L3CipheringModeCommand::writeBody(L3Frame& frame, size_t& wp) const
{
	// reverse order of 1/2-octet fields
	mCipheringResponse.writeV(frame,wp);
	mCipheringModeSetting.writeV(frame,wp);
}

void L3CipheringModeCommand::text(ostream& os) const
{
	L3RRMessage::text(os);
	os << "ciphering mode setting=(" << mCipheringModeSetting << ")";
	os << " ciphering response=(" << mCipheringResponse << ")";
}

int L3CipheringModeComplete::MTI() const
{
	return CipheringModeComplete;
}

void L3CipheringModeComplete::parseBody(const L3Frame& frame, size_t& rp)
{
	// mobile equipment identity optional
}

void L3CipheringModeComplete::text(ostream& os) const
{
	L3RRMessage::text(os);
}

void L3PhysicalInformation::writeBody(L3Frame& frame, size_t& wp) const
{
	mTA.writeV(frame,wp);
}

void L3PhysicalInformation::text(ostream& os) const
{
	L3RRMessage::text(os);
	os << "TA=" << mTA;
}




// vim: ts=4 sw=4
