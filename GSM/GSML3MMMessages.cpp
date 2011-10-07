/**@file
	@brief GSM Mobility Management messages, from GSM 04.08 9.2.
*/

/*
* Copyright 2008-2010 Free Software Foundation, Inc.
* Copyright 2010 Kestrel Signal Processing, Inc.
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

#include "GSML3CommonElements.h"
#include "GSML3MMMessages.h"
#include <Logger.h>


using namespace std;
using namespace GSM;



ostream& GSM::operator<<(ostream& os, L3MMMessage::MessageType val)
{
	switch (val) {
		case L3MMMessage::IMSIDetachIndication: 
			os << "IMSI Detach Indication"; break;
		case L3MMMessage::CMServiceRequest: 
			os << "CM Service Request"; break;
		case L3MMMessage::CMServiceAccept: 
			os << "CM Service Accept"; break;
		case L3MMMessage::CMServiceReject: 
			os << "CM Service Reject"; break;
		case L3MMMessage::CMServiceAbort: 
			os << "CM Service Abort"; break;
		case L3MMMessage::CMReestablishmentRequest:
			os << "CM Re-establishment Request"; break;
		case L3MMMessage::IdentityResponse: 
			os << "Identity Response"; break;
		case L3MMMessage::IdentityRequest: 
			os << "Identity Request"; break;
		case L3MMMessage::MMInformation: 
			os << "MM Information"; break;
		case L3MMMessage::LocationUpdatingAccept: 
			os << "Location Updating Accept"; break;
		case L3MMMessage::LocationUpdatingReject: 
			os << "Location Updating Reject"; break;
		case L3MMMessage::LocationUpdatingRequest: 
			os << "Location Updating Request"; break;
		case L3MMMessage::MMStatus: 
			os << "MM Status"; break;
		case L3MMMessage::AuthenticationRequest:
			os << "Authentication Request"; break;
		case L3MMMessage::AuthenticationResponse:
			os << "Authentication Response"; break;
		default: os << hex << "0x" << (int)val << dec;
	}
	return os;
}




L3MMMessage* GSM::L3MMFactory(L3MMMessage::MessageType MTI)
{
	switch (MTI) {
	  case L3MMMessage::LocationUpdatingRequest: return new L3LocationUpdatingRequest;
	  case L3MMMessage::IMSIDetachIndication: return new L3IMSIDetachIndication;
	  case L3MMMessage::CMServiceRequest: return new L3CMServiceRequest;
	  // Since we don't support re-establishment, don't bother parsing this.
	  //case L3MMMessage::CMReestablishmentRequest: return new L3CMReestablishmentRequest;
	  case L3MMMessage::MMStatus: return new L3MMStatus;
	  case L3MMMessage::IdentityResponse: return new L3IdentityResponse;
	  case L3MMMessage::AuthenticationResponse: return new L3AuthenticationResponse;
	  default:
	    LOG(WARNING) << "no L3 MM factory support for message " << MTI;
		return NULL;
	}
}

L3MMMessage * GSM::parseL3MM(const L3Frame& source)
{
	L3MMMessage::MessageType MTI = (L3MMMessage::MessageType)(0xbf & source.MTI());
	LOG(DEBUG) << "parseL3MM MTI=" << MTI;

	L3MMMessage *retVal = L3MMFactory(MTI);
	if (retVal==NULL) return NULL;

	retVal->parse(source);
	return retVal;
}



void L3MMMessage::text(ostream& os) const
{
	os << "MM " << (MessageType) MTI() << " ";
}


void L3LocationUpdatingRequest::parseBody( const  L3Frame &src, size_t &rp )
{
		// skip updating type
		rp += 4;
		// skip ciphering ket sequence number
		rp += 4;
		mLAI.parseV(src,rp);
		mClassmark.parseV(src,rp);
		mMobileIdentity.parseLV(src, rp);
}


void L3LocationUpdatingRequest::text(ostream& os) const
{
	L3MMMessage::text(os);
	os << "LAI=("<<mLAI<<")";
	os << " MobileIdentity=("<<mMobileIdentity<<")";
	os << " classmark=(" << mClassmark << ")";
}


size_t L3LocationUpdatingRequest::l2BodyLength() const
{
	size_t len = 0;
	len += 1;	// updating type and ciphering key seq num
	len += mLAI.lengthV();
	len += mClassmark.lengthV();
	len += mMobileIdentity.lengthLV(); 
	return len;
}



size_t L3LocationUpdatingAccept::l2BodyLength() const
{
	if (mHaveMobileIdentity) return mLAI.lengthV() + mMobileIdentity.lengthTLV();
	else return mLAI.lengthV();
}

void L3LocationUpdatingAccept::writeBody( L3Frame &dest, size_t &wp ) const
{
	mLAI.writeV(dest, wp);
	if (mHaveMobileIdentity) mMobileIdentity.writeTLV(0x17,dest,wp);
}

void L3LocationUpdatingAccept::text(ostream& os) const
{
	L3MMMessage::text(os);
	os<<"LAI=("<<mLAI<<")";
	if (mHaveMobileIdentity) os << "ID=(" << mMobileIdentity << ")";
}


void L3LocationUpdatingReject::writeBody( L3Frame &dest, size_t &wp ) const 
{
	mRejectCause.writeV(dest, wp);
}

void L3LocationUpdatingReject::text(ostream& os) const
{
	L3MMMessage::text(os);
	os <<"cause="<<mRejectCause;
}



void L3IMSIDetachIndication::parseBody(const L3Frame& src, size_t &rp)
{
	mClassmark.parseV(src, rp);
	mMobileIdentity.parseLV(src, rp);
}

void L3IMSIDetachIndication::text(ostream& os) const
{
	L3MMMessage::text(os);
	os << "mobileID=(" << mMobileIdentity << ")";
	os << " classmark=(" << mClassmark << ")";
}



void L3CMServiceReject::writeBody(L3Frame& dest, size_t &wp) const
{
	mCause.writeV(dest,wp);
}

void L3CMServiceReject::text(ostream& os) const
{
	L3MMMessage::text(os);
	os << "cause=" << mCause;
}



void L3CMServiceRequest::parseBody( const L3Frame &src, size_t &rp )
{
	rp += 4;			// skip ciphering key seq number
	mServiceType.parseV(src,rp);
	mClassmark.parseLV(src,rp);
	mMobileIdentity.parseLV(src, rp);
	// ignore priority
}

void L3CMServiceRequest::text(ostream& os) const
{
	L3MMMessage::text(os);
	os << "serviceType=" << mServiceType;
	os << " mobileIdentity=("<<mMobileIdentity<<")";
	os << " classmark=(" << mClassmark << ")";
}




void L3CMReestablishmentRequest::parseBody(const L3Frame& src, size_t &rp)
{
	rp += 8;			// skip ciphering
	mClassmark.parseLV(src,rp);
	mMobileID.parseLV(src,rp);
	mHaveLAI = mLAI.parseTLV(0x13,src,rp);
}

void L3CMReestablishmentRequest::text(ostream& os) const
{
	L3MMMessage::text(os);
	os << "mobileID=(" << mMobileID << ")";
	if (mHaveLAI) os << " LAI=(" << mLAI << ")";
	os << " classmark=(" << mClassmark << ")";
}


void L3MMStatus::parseBody( const L3Frame &src, size_t &rp)
{
	mRejectCause.parseV(src, rp);
}

void L3MMStatus::text(ostream& os) const 
{
	L3MMMessage::text(os);
	os << " RejectCause= <"<<mRejectCause<<">";
}


size_t L3MMInformation::l2BodyLength() const
{
	size_t len=0;
	if (mShortName.lengthV()>1) len += mShortName.lengthTLV();
	len += mTime.lengthTV();
	return len;
}


void L3MMInformation::writeBody(L3Frame &dest, size_t &wp) const
{
	if (mShortName.lengthV()>1) mShortName.writeTLV(0x45,dest,wp);
	mTime.writeTV(0x47,dest,wp);
}


void L3MMInformation::text(ostream& os) const
{
	L3MMMessage::text(os);
	os << "short name=(" << mShortName << ")";
	os << " time=(" << mTime << ")";
}


void L3IdentityRequest::writeBody(L3Frame& dest, size_t &wp) const
{
	dest.writeField(wp,0,4);		// spare half octet
	dest.writeField(wp,mType,4);
}


void L3IdentityRequest::text(ostream& os) const
{
	L3MMMessage::text(os);
	os << "type=" << mType;
}



void L3IdentityResponse::parseBody(const L3Frame& src, size_t& rp)
{
	mMobileID.parseLV(src,rp);
}

void L3IdentityResponse::text(ostream& os) const
{
	L3MMMessage::text(os);
	os << "mobile id=" << mMobileID;
}



void L3AuthenticationRequest::writeBody(L3Frame& dest, size_t &wp) const
{
	dest.writeField(wp,0,4);		// spare half octet
	mCipheringKeySequenceNumber.writeV(dest,wp);
	mRAND.writeV(dest,wp);
}

void L3AuthenticationRequest::text(ostream& os) const
{
	L3MMMessage::text(os);
	os << "cksn=" << mCipheringKeySequenceNumber;
	os << " RAND=" << mRAND;
}



void L3AuthenticationResponse::parseBody(const L3Frame& src, size_t& rp)
{
	mSRES.parseV(src,rp);
}

void L3AuthenticationResponse::text(ostream& os) const
{
	L3MMMessage::text(os);
	os << "SRES=" << mSRES;
}



// vim: ts=4 sw=4
