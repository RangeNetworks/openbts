/*
* Copyright 2008 Free Software Foundation, Inc.
* Copyright 2011, 2014 Range Networks, Inc.
*
* This software is distributed under multiple licenses; see the COPYING file in the main directory for licensing information for this specific distribution.
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
#include <MemoryLeak.h>

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
	string msmRoutes;		// Route headers.
	string msmRecordRoutes;	// Record-route headers.
	string msmContactValue;
	//list<SipBody> msmBodies;
	string msmContentType;
	string msmBody;
	int msmCode;
	string msmReason;	// This is the reason phrase from the first line, not the "Reason: Header".
	string msmReasonHeader;	// This is the "Reason:" header.
	int msmCSeqNum;
	string msmCSeqMethod;
	SipParamList msmHeaders;	// Other unrecognized headers.  We use SipParamList because its the same.
	string msmAuthenticateValue;	// The www-authenticate value as a string.
	string msmAuthorizationValue;	// The Authorization value as a string.
	string msmMaxForwards;			// Must be a string so we can detect unset as a special case.
	SipTermList SIPMsgCallTerminationList;			// List of SIP termination reasons

	// Modifiers:
	void smInit() { msmMaxForwards = string("70"); }
	void smAddViaBranch(string transport, string branch);
	void smAddViaBranch(SipBase *dialog,string branch);
	void smAddViaBranch3(string transport, string proxy, string branch);
	string smPopVia();		// Pop and return the top via; modifies msmVias.
	void smCopyTopVia(SipMessage *other);
	void smAddBody(string contentType, string body);

	SipMessage() : msmCode(0), msmCSeqNum(0), SIPMsgCallTerminationList() {}	// mCSeq will be filled in later.

	void smAddHeader(string name, string value) {
		SipParam param(name,value);
		msmHeaders.push_back(param);
	}
	bool smDecrementMaxFowards();

	string smGenerate(string userAgent);	// Recreate the message from fields; return result and also leave it in msmContent.

	SipTermList& getTermList() { return SIPMsgCallTerminationList; }
	void addCallTerminationReasonSM(CallTerminationCause::termGroup group, int cause, string desc);

	// Accessors:
	bool isRequest() { return msmCode == 0; }
	bool isRequestNotAck() { return msmCode == 0 && strcasecmp(msmReqMethod.c_str(),"ACK"); }
	int smCSeqNum() const { return msmCSeqNum; }
	string smGetReturnIPAndPort();
	string smGetRemoteTag() { return isRequest() ? msmFrom.mTag : msmTo.mTag; }
	string smGetLocalTag() { return isRequest() ? msmTo.mTag : msmFrom.mTag; }
	string smCSeqMethod() const { return msmCSeqMethod; }
	string smGetToHeader() const { return msmTo.value(); }
	string smUriUsername();	// The username in an originating request.
	string smGetInviteImsi();
	string smGetMessageBody() const;
	string smGetMessageContentType() const;
	string smGetBranch();		// The branch from the top via.
	string smGetProxy() const;	// The proxy from the top via.
	const char *smGetMethodName() const { return msmReqMethod.c_str(); }
	const string smGetReason() const { return msmReason; }
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
