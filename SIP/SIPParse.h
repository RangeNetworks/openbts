/*
* Copyright 2013, 2014 Range Networks, Inc.
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
// Written by Pat Thompson.
#ifndef _SIPPARSE_H_
#define _SIPPARSE_H_ 1
#include <string>
#include <Defines.h>
#include <list>
#include <Logger.h>
//#include "SIPBase.h"


namespace SIP {
using namespace std;
class SipMessage;
class SipBase;
class DialogStateVars;

extern string makeUriWithTag(string username, string ip, string tag);
extern string makeUri(string username, string ip, unsigned port=0);
extern size_t trimrightn(const char *startp, size_t endpos, const char *trimchars=" \t\r\n");
extern size_t trimleftn(const char *startp, size_t startpos=0, const char *trimchars=" \t\r\n");
extern string trimboth(string input, const char *trimchars=" \t\r\n");
extern void commaListPushFront(string *cl, string val);
extern void commaListPushBack(string *cl, string val);
extern string commaListFront(string cl);
extern string commaListPopFront(string *cl);

// We use this for headers also.
struct SipParam {
	string mName, mValue;
	SipParam(string wName, string wValue) : mName(wName), mValue(wValue) {}
	SipParam() {}
};
struct SipParamList : public list<SipParam> {
	SipParamList::iterator paramFindIt(const char *name);
	string paramFind(const char *name);
	string paramFind(string name) { return paramFind(name.c_str()); }
};

string parseURI(const char *buffer, SipParamList &params, SipParamList &headers);

// RFC2396 describes URI generic syntax.
// This is the subset of a full URI that we use.
// We dont care about URI params or headers.
// Note that the tag is a From or To generic-parameter, not a URI-parameter.
class SipUri : public string {		// The base class string contains the full URI.
	static const unsigned start = 4;		// strlen("sip:")
	public:
	string uriValue() const { return substr(); }	// return the whole thing.
	string uriUsername() const {	// username without "sip:" or the host or password.
		if (size() < start) return string("");
		string result = substr(start,find_first_of(";@:",start)-start);	// chop off :password or @host or if no @host, ;params
		//LOG(DEBUG) << LOGVAR2("uri",substr())<<LOGVAR(n)<<LOGVAR(result) <<LOGVAR(find_first_of('@'))<<LOGVAR(uriAddress());
		return result;
	}
	string uriAddress() const {	// without sip: or the params or headers.
		if (size() < start) return string("");
		return substr(start,find_first_of(";&",start)-start);
	}
	string uriHostAndPort() const {
		string addr = uriAddress();
		size_t where = addr.find_first_of('@');
		return (where == string::npos) ? string("") : addr.substr(where+1);
	}
	// Create a uri from another full uri.  If it is <uri> chop off the < and >
	void uriSet(string fullUri) {
		if (fullUri.size() == 0) { clear(); return; }	// Dont let fullUri[0] will throw an exception.
		if (fullUri[0] == '<') {
			assign(fullUri.substr(1,fullUri.rfind('>')-1));
		} else {
			assign(fullUri);
		}
	}
	// Create a uri from a username and host.
	//void uriSetName(string username,string host, unsigned port=0) {		// The host could also contain a port spec; we dont care.
	//	assign(makeUri(username,host,port));
	//}
	SipUri() {}
	SipUri(string wFullUri) { uriSet(wFullUri); }
};

struct SipVia : string {
	string mSentBy;
	string mViaBranch;
	void viaParse(string);
	SipVia(string vialine) { viaParse(vialine); }
};

// This is a From: or To:
class SipPreposition {
	friend class DialogStateVars;
	friend class SipMessage;
	string mFullHeader;
	string mDisplayName;
	SipUri mUri;
	string mTag;
	void rebuild();
	public:
	void prepSet(const string header);	// immediately parses the string as a Contact.
	void prepSetUri(string uri) {
		mUri.uriSet(uri);
		rebuild();
	}
	void prepSetDisplayName(string displayName) {
		mDisplayName = displayName;
		rebuild();
	}
	void setTag(string wTag) { mTag = wTag; rebuild(); }
	string getTag() const { return mTag; }
	string value() const { return mFullHeader; }
	string toFromValue() const { return mFullHeader; }
	// Header looks like this: To/From: mDisplayName <uriUsername@uriHostAndPort>;tag=mTag
	string uriUsername() const { return mUri.uriUsername(); }
	string uriHostAndPort() const { return mUri.uriHostAndPort(); }

	SipPreposition(string wDisplayName,string wUri, string wTag="") : mDisplayName(wDisplayName), mUri(wUri), mTag(wTag) {
		rebuild();
	}
	SipPreposition(string fullHeader) { prepSet(fullHeader); }
	SipPreposition() {}
};
// TODO: operator<<

struct SdpInfo {
	unsigned sdpRtpPort;
	string sdpUsername;
	string sdpHost;
	string sdpSessionId;
	string sdpVersionId;
	string sdpCodecList;		// An offer has multiple numbers here, a reply has just one.
	string sdpAttrs;

	void sdpParse(const char *buffer);
	void sdpParse(string sdp) { sdpParse(sdp.c_str()); }
	void sdpInitOffer(const SipBase *dialog);
	void sdpInitRefer(const SipBase *dialog, int remotePort);
	string sdpValue();
};


/*
 * Single entry in the Call termination cause
 * SIPCallTermination
 */
class CallTerminationCause {
public:

	enum termGroup {
		eNone = 0,
		eTermSIP,
		eQ850
	};

	string getQ850CallTermText(int l3Cause);
	string getSIPCallTermText(int iSIPError);

	// (pat) The L3Cause::Preemption does not map into a Q850 cause, so this can not be copied directly.
	CallTerminationCause(CallTerminationCause::termGroup group, int cause, string desc) {
		//LOG(INFO) << "SIP term info CallTerminationCause begin ctor, group: " << group << " cause: "<< cause << " text: " << desc;

		mReasonGroup = group;
		miCause = cause;
		if (desc == "") {
			if (mReasonGroup == eQ850) {
				msCauseDesc = getQ850CallTermText(miCause);
			} else if (mReasonGroup == eTermSIP) {
				msCauseDesc = getSIPCallTermText(miCause);
			}
		} else {
			msCauseDesc = desc;
		}
		//LOG(INFO) << "SIP term info CallTerminationCause end ctor, group: " << mReasonGroup << " cause: "<< miCause << " text: " << msCauseDesc;
	}

	// Copy ctor
	CallTerminationCause(const CallTerminationCause& Source) {
		mReasonGroup = Source.mReasonGroup;
		miCause = Source.miCause;
		if (Source.msCauseDesc == "") {
			if (mReasonGroup == eQ850) {
				msCauseDesc = getQ850CallTermText(miCause);
			} else if (mReasonGroup == eTermSIP) {
				msCauseDesc = getSIPCallTermText(miCause);
			}
		} else {
			msCauseDesc = Source.msCauseDesc;
		}

		//LOG(INFO) << "SIP term info CallTerminationCause copy ctor, group: " << mReasonGroup << " cause: "<< miCause << " text: " << msCauseDesc;
		//LOG(INFO) << "SIP term info text in copy ctor from getTextforEntry: " << this->getTextforEntry();
	}

	CallTerminationCause() {
		//LOG(INFO) << "SIP term info CallTerminationCause no param ctor";
		mReasonGroup = eNone;
		miCause = 0;
		msCauseDesc = "Default";
	}

	string getTermGroupText() {
		if (mReasonGroup == eTermSIP)
			return "SIP";
		else if (mReasonGroup == eQ850)
			return "Q.850";
		else {
			string sTemp = format("Unknown reason group: %d", mReasonGroup);
			return sTemp;
		}
	}

	CallTerminationCause::termGroup getTermGroup() { return mReasonGroup; }

	int getTermCause() { return miCause; }

	string getCauseDescription() { return msCauseDesc; }

	// Return text for one entry should look like the examples below
	// Reason: SIP ;cause=580 ;text="Precondition Failure"
	// Reason: Q.850 ;cause=16 ;text="Terminated"
	string getTextforEntry() {
		string sTemp;
		//sTemp = "Reason: " + getTermGroupText() + "; cause=" +  to_string(icause) + "; text=\"" + getcauseDescription() + "\"\r\n";  //SVGDBG could not get this to compile
		sTemp = format("Reason:%s; cause=%d; text=\"%s\"\r\n",  this->getTermGroupText().c_str(), miCause, msCauseDesc.c_str());
		return sTemp;
	}

private:

	/*
	SIP: The cause parameter contains a SIP status code.  See section 21 in RFC 3261 Q.850: The cause parameter contains an
	ITU-T Q.850 cause value in decimal representation.  */

	termGroup mReasonGroup; // SIP or Q.850 reason
	int miCause; // Numerical code
	string msCauseDesc;
}; // CallTerminationCause


/*  (pat 7-2014) This is no longer being used.
List of termination reasons. It is valid to 0 to N
SipTermList
Example:
	SipBase::addCallTerminationReasonDlg(CallTerminationCause(CallTerminationCause::eTermSIP/eQ850, 100, "This is an error"));
	SipMessage::addCallTerminationReasonSM(CallTerminationCause(CallTerminationCause::eTermSIP/eQ850, 100, "This is an error"));
	CallTerminationCause sc(CallTerminationCause::eQ850, (int) GSM::L3Cause::NormalCallClearing, "");
	addCallTerminationReasonSM(sc);
*/


class SipTermList {
public:
	SipTermList() {
		//LOG(INFO) << "SIP term info SipTermList ctor addr: "  << (void*) this;
	}

	// Copy ctor since SIP messages get copied
	SipTermList(const SipTermList& Source) : lTermList(){
		LOG(INFO) << "SIP term info SipTermList copy ctor";
		for (vector<CallTerminationCause*>::const_iterator it = Source.lTermList.begin(); it != Source.lTermList.end(); it++) {
			lTermList.push_back(new CallTerminationCause( *(*it)) );
		} // for
	}


	void add(CallTerminationCause* pEntry) {
		if (((int) pEntry->getTermGroup() != 0) && (pEntry->getTermCause() != 0)) {
			//LOG(INFO) << "SIP term info adding entry to call termination list: " << pEntry->getTextforEntry();  // SVGDBG
			//if (lTermList.size() > 1) {
				// Added more that one entry log this so we can make sure this is correct
			//	LOG(INFO) << "SIP term info adding entry to call termination reason, table size: " << lTermList.size();  // SVGDBG
			//}
			lTermList.push_back(pEntry);
			//LOG(INFO) << "SIP term info adding entry to call termination reason, table size: " << lTermList.size();  // SVGDBG
		} else {
			LOG(INFO) << "SIP term info tried to add termination reason with invalid data, term group: " \
					<< pEntry->getTermGroup() << " term cause: " << pEntry->getTermCause() << " desc: " << pEntry->getCauseDescription();
		}
		logList(); // SVGDBG
	}


	void add(CallTerminationCause::termGroup group, int cause, string desc) {
		CallTerminationCause* pCause = new CallTerminationCause(group, cause, desc);
		add(pCause);
	}

	void logList() {
		LOG(INFO) << "SIP term info List terminate table addr: " << (void*) this;
		for (vector<CallTerminationCause*>::iterator it = lTermList.begin(); it != lTermList.end(); it++) {
			LOG(INFO) << "SIP term info list entry: " << (*it)->getTextforEntry(); // SVGDBG
		} // for
	}

	string getTextForAllMsgs() {
		string sTemp;
		for (vector<CallTerminationCause*>::iterator it = lTermList.begin(); it != lTermList.end(); it++) {
			sTemp = sTemp + (*it)->getTextforEntry();
		} // for
		return sTemp;
	}

	// Clear list delete all entries and empty list
	void clearList() {
		LOG(INFO) << "SIP term info clear list";
		for (vector<CallTerminationCause*>::iterator it = lTermList.begin(); it != lTermList.end(); it++) {
			//LOG(INFO) << "SIP term info clearList remove entry: " << (*it)->getTextforEntry(); // SVGDBG
			delete (*it);
		}
		lTermList.clear();
	}

	// Used to copy from SIPDialog to SIPMessage
	// Remove entries from source list so they don't get copied again
	void copyEntireList(SipTermList &lDestTermList) {
		LOG(INFO) << "SIP term info copyEntireList source size: " << lTermList.size();
		for (vector<CallTerminationCause*>::iterator it = lTermList.begin(); it != lTermList.end(); it++) {
			lDestTermList.add(new CallTerminationCause(*(*it)));
		} // for

		clearList(); // Remove all entries from list
	}

	~SipTermList() {
		//LOG(INFO) << "SIP term info SipTermList dtor address: " << (void*) this; // SVGDBG
		logList();

		for (vector<CallTerminationCause*>::iterator it = lTermList.begin(); it != lTermList.end(); it++) {
			//LOG(INFO) << "SIP term info SipTermList dtor remove entry: " << (*it)->getTextforEntry(); // SVGDBG
			delete (*it);
		}
	}

	int size() { return lTermList.size(); }


//private: // SVGDBG
	vector<CallTerminationCause*> lTermList;
};


extern SipMessage *sipParseBuffer(const char *buffer);
extern void parseAuthenticate(string stuff, SipParamList &params);
extern void parseToParams(string stuff, SipParamList &params);
extern bool crackUri(string header, string *before, string *uri, string *tail);

}; // namespace
#endif
