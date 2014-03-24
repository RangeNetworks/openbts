/*
* Copyright 2008 Free Software Foundation, Inc.
* Copyright 2011 Range Networks, Inc.
*
* This software is distributed under multiple licenses; see the COPYING file in the main directory for licensing information for this specific distribuion.
*
* This use of this software may be subject to additional restrictions.
* See the LEGAL file in the main directory for details.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

*/




#ifndef _SIP2MESSAGE_H_
#define _SIP2MESSAGE_H_
#include <string>
#include "SIPParse.h"
#include "config.h"		// For VERSION

#include <ControlTransfer.h>	// For CodecSet

namespace SIP {
using namespace std;
class SipDialog;
class SipBase;


//struct SipBody {
//	string mContentType;
//	string mBody;
//	SipBody(string wContentType,string wBody) : mContentType(wContentType),mBody(wBody) {}
//};

DEFINE_MEMORY_LEAK_DETECTOR_CLASS(SipMessage,MemCheckSipMessage)
class SipMessage : public MemCheckSipMessage {
	friend ostream& operator<<(ostream& os, const SipMessage*msg);
	friend ostream& operator<<(ostream& os, const SipMessage&msg);
	public:
	string msmContent;		// The full message content as a string of chars.

	string msmCallId;		// Put in every message to identify dialog.
	// Historically we use the callid without the @host for dialog identification purposes.  (Which is wrong.)
	//string smGetCallId() const { return string(msmCallId,0,msmCallId.find_first_of('@')); }
	string smGetCallId() const { return msmCallId; }

	string msmReqUri;
	SipPreposition msmTo;
	SipPreposition msmFrom;
	string msmReqMethod;
	string msmVias;		// Via lines, including the first one with mViaBranch and mViaSentBy broken out.
	string msmRoutes;		// Via lines, including the first one with mViaBranch and mViaSentBy broken out.
	string msmRecordRoutes;		// Via lines, including the first one with mViaBranch and mViaSentBy broken out.
	string msmContactValue;
	//list<SipBody> msmBodies;
	string msmContentType;
	string msmBody;
	int msmCode;
	string msmReason;
	int msmCSeqNum;
	string msmCSeqMethod;
	SipParamList msmHeaders;	// Other unrecognized headers.  We use SipParamList because its the same.
	string msmAuthenticateValue;	// The www-authenticate value as a string.
	string msmAuthorizationValue;	// The Authorization value as a string.

	void smAddViaBranch(string transport, string branch);
	void smAddViaBranch(SipBase *dialog,string branch);
	string smGetBranch();
	string smGetReturnIPAndPort();
	void smCopyTopVia(SipMessage *other);
	void smAddBody(string contentType, string body);
	string smGenerate();
	SipMessage() : msmCode(0), msmCSeqNum(0) {}	// mCSeq will be filled in later.
	int smCSeqNum() const { return msmCSeqNum; }
	void smAddHeader(string name, string value) {
		SipParam param(name,value);
		msmHeaders.push_back(param);
	}

	// Accessors:
	bool isRequest() { return msmCode == 0; }
	string smGetRemoteTag() { return isRequest() ? msmFrom.mTag : msmTo.mTag; }
	string smGetLocalTag() { return isRequest() ? msmTo.mTag : msmFrom.mTag; }
	string smCSeqMethod() const { return msmCSeqMethod; }
	string smGetToHeader() const { return msmTo.value(); }
	string smUriUsername();
	string smGetInviteImsi();
	string smGetMessageBody() const;
	string smGetMessageContentType() const;
	string smGetProxy() const {
		string topvia = commaListFront(msmVias);
		if (topvia.empty()) { return string(""); }	// oops
		SipVia via(topvia);
		return via.mSentBy;
	}
	const char *smGetMethodName() const { return msmReqMethod.c_str(); }
	const char *smGetReason() const { return msmReason.c_str(); }
	string smGetRand401();
	int smGetCode() const { return msmCode; }
	int smGetCodeClass() const { return (msmCode / 100) * 100; }

	// Other accessors.
	string smGetFirstLine() const;
	string smGetPrecis() const;		// A short message description for messages.
	string text(bool verbose = false) const;

	// These get inlined.
	bool isINVITE() const { return !strcmp(smGetMethodName(),"INVITE"); }
	bool isMESSAGE() const { return !strcmp(smGetMethodName(),"MESSAGE"); }
	bool isCANCEL() const { return !strcmp(smGetMethodName(),"CANCEL"); }
	bool isBYE() const { return !strcmp(smGetMethodName(),"BYE"); }
	bool isACK() const { return !strcmp(smGetMethodName(),"ACK"); }
	// Has the message been initialized yet?  We always add callid first thing, so check that.
	bool smIsEmpty() const { return msmContent.empty() && msmCallId.empty(); }
};

bool sameMsg(SipMessage *msg1, SipMessage *msg2);


struct SipMessageAckOrCancel : SipMessage {
	SipMessageAckOrCancel(string method, SipMessage *other);
};

struct SipMessageRequestWithinDialog : SipMessage {
	SipMessageRequestWithinDialog(string reqMethod, SipBase *dialog, string branch="");
};

struct SipMessageReply : SipMessage {
	SipMessageReply(SipMessage *request,int code, string reason, SipBase *dialog);
};

struct SipMessageHandoverRefer : SipMessage {
	SipMessageHandoverRefer(const SipBase *dialog, string peer);
};

ostream& operator<<(ostream& os, const SipMessage&msg);
ostream& operator<<(ostream& os, const SipMessage*msg);
extern const char *extractIMSI(const char *imsistring);
};
#endif
