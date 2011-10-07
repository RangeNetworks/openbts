/**@file
  @brief GSM Radio Resorce messages, from GSM 04.08 9.1.
*/
/*
* Copyright 2008, 2009, 2010, 2011 Free Software Foundation, Inc.
* Copyright 2011 Kestrel Signal Processing, Inc.
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




#include <typeinfo>
#include <iostream>

#include "GSML3RRMessages.h"
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





ostream& GSM::operator<<(ostream& os, L3RRMessage::MessageType val)
{
	switch (val) {
		case L3RRMessage::SystemInformationType1: 
			os << "System Information Type 1"; break;
		case L3RRMessage::SystemInformationType2: 
			os << "System Information Type 2"; break;
		case L3RRMessage::SystemInformationType2bis: 
			os << "System Information Type 2bis"; break;
		case L3RRMessage::SystemInformationType2ter: 
			os << "System Information Type 2ter"; break;
		case L3RRMessage::SystemInformationType3: 
			os << "System Information Type 3"; break;
		case L3RRMessage::SystemInformationType4: 
			os << "System Information Type 4"; break;
		case L3RRMessage::SystemInformationType5: 
			os << "System Information Type 5"; break;
		case L3RRMessage::SystemInformationType5bis: 
			os << "System Information Type 5bis"; break;
		case L3RRMessage::SystemInformationType5ter: 
			os << "System Information Type 5ter"; break;
		case L3RRMessage::SystemInformationType6: 
			os << "System Information Type 6"; break;
		case L3RRMessage::SystemInformationType7: 
			os << "System Information Type 7"; break;
		case L3RRMessage::SystemInformationType8: 
			os << "System Information Type 8"; break;
		case L3RRMessage::SystemInformationType9: 
			os << "System Information Type 9"; break;
		case L3RRMessage::SystemInformationType13: 
			os << "System Information Type 13"; break;
		case L3RRMessage::SystemInformationType16: 
			os << "System Information Type 16"; break;
		case L3RRMessage::SystemInformationType17: 
			os << "System Information Type 17"; break;
		case L3RRMessage::PagingResponse: 
			os << "Paging Response"; break;
		case L3RRMessage::PagingRequestType1: 
			os << "Paging Request Type 1"; break;
		case L3RRMessage::MeasurementReport: 
			os << "Measurement Report"; break;
		case L3RRMessage::AssignmentComplete: 
			os << "Assignment Complete"; break;
		case L3RRMessage::ImmediateAssignment: 
			os << "Immediate Assignment"; break;
		case L3RRMessage::ImmediateAssignmentReject: 
			os << "Immediate Assignment Reject"; break;
		case L3RRMessage::AssignmentCommand: 
			os << "Assignment Command"; break;
		case L3RRMessage::AssignmentFailure: 
			os << "Assignment Failure"; break;
		case L3RRMessage::ChannelRelease: 
			os << "Channel Release"; break;
		case L3RRMessage::ChannelModeModify:
			os << "Channel Mode Modify"; break;
		case L3RRMessage::ChannelModeModifyAcknowledge:
			os << "Channel Mode Modify Acknowledge"; break;
		case L3RRMessage::GPRSSuspensionRequest: 
			os << "GPRS Suspension Request"; break;
		case L3RRMessage::ClassmarkEnquiry: 
			os << "Classmark Enquiry"; break;
		case L3RRMessage::ClassmarkChange: 
			os << "Classmark Change"; break;
		case L3RRMessage::RRStatus:
			os << "RR Status"; break;
		case L3RRMessage::ApplicationInformation:
			os << "Application Information"; break;
		default: os << hex << "0x" << (int)val << dec;
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
	// channel needed codes
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
		os << "(" << mMobileIDs[i] << "," << mChannelsNeeded[i] << "),";
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
	if (mHaveRestOctets) mRestOctets.writeV(dest,wp);
	LOG(DEBUG) << dest;
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
	if (mHaveRestOctets) os << " SI3RO=(" << mRestOctets << ")";
}



L3SystemInformationType4::L3SystemInformationType4()
	:L3RRMessageNRO()
{ }



size_t L3SystemInformationType4::l2BodyLength() const
{
	size_t len = mLAI.lengthV();
	len += mCellSelectionParameters.lengthV();
	len += mRACHControlParameters.lengthV();
	return len;
}


void L3SystemInformationType4::writeBody(L3Frame& dest, size_t &wp) const
{
/*
	System Information Type 4, GSM 04.08 9.1.36
	- Location Area Identification 10.5.1.3 M V 5 
	- Cell Selection Parameters 10.5.2.4 M V 2 
	- RACH Control Parameters 10.5.2.29 M V 3 
*/
	mLAI.writeV(dest,wp);
	mCellSelectionParameters.writeV(dest,wp);
	mRACHControlParameters.writeV(dest,wp);
}


void L3SystemInformationType4::text(ostream& os) const
{
	L3RRMessage::text(os);
	os << "LAI=(" << mLAI << ")";
	os << " cellSelectionParameters=(" << mCellSelectionParameters << ")";
	os << " RACHControlParameters=(" << mRACHControlParameters << ")";
}




void L3SystemInformationType5::writeBody(L3Frame& dest, size_t &wp) const
{
/*
	System Information Type 5, GSM 04.08 9.1.37
	- BCCH Frequency List 10.5.2.22 M V 16 
*/
	mBCCHFrequencyList.writeV(dest,wp);
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


void L3ImmediateAssignment::writeBody( L3Frame &dest, size_t &wp ) const
{
/*
- Page Mode 10.5.2.26 M V 1/2 
- Dedicated mode or TBF 10.5.2.25b M V 1/2 
- Channel Description 10.5.2.5 C V 3 
- Request Reference 10.5.2.30 M V 3 
- Timing Advance 10.5.2.40 M V 1 
(ignoring optional elements)
*/
	// reverse order of 1/2-octet fields
	mDedicatedModeOrTBF.writeV(dest, wp);
	mPageMode.writeV(dest, wp);
	mChannelDescription.writeV(dest, wp);
	mRequestReference.writeV(dest, wp);
	mTimingAdvance.writeV(dest, wp);
	// No mobile allocation in non-hopping systems.
	// A zero-length LV.  Just write L=0.
	dest.writeField(wp,0,8);
}


void L3ImmediateAssignment::text(ostream& os) const
{
	os << "PageMode=("<<mPageMode<<")";
	os << " DedicatedModeOrTBF=("<<mDedicatedModeOrTBF<<")";
	os << " ChannelDescription=("<<mChannelDescription<<")";
	os << " RequestReference=("<<mRequestReference<<")";
	os << " TimingAdvance="<<mTimingAdvance;
}


void L3ChannelRequest::text(ostream& os) const
{
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
}

size_t L3AssignmentCommand::l2BodyLength() const 
{
	size_t len = mChannelDescription.lengthV();
	len += mPowerCommand.lengthV();
	if (mHaveMode1) len += mMode1.lengthTV();
	return len;
}


void L3AssignmentCommand::text(ostream& os) const
{
	L3RRMessage::text(os);
	os <<"channelDescription=("<<mChannelDescription<<")";
	os <<" powerCommand="<<mPowerCommand;
	if (mHaveMode1) os << " mode1=" << mMode1;
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
}


void L3ChannelModeModify::text(ostream& os) const
{
	L3RRMessage::text(os);
	os << "description=(" << mDescription << ")";
	os << " mode=(" << mMode << ")";
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
    L3ApplicationInformation(BitVector& data, unsigned protocolIdentifier,
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
	mFlags.parseV(src, rp);
	mID.parseV(src, rp);
	mData.parseLV(src, rp);
}

size_t L3ApplicationInformation::l2BodyLength() const
{
	return 1 + mData.lengthLV();
}


void L3GPRSSuspensionRequest::parseBody(const L3Frame &src, size_t& rp)
{
	// We don't really parse this yet.
	return;
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

// vim: ts=4 sw=4
