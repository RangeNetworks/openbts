/*
* Copyright 2013 Range Networks, Inc.
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
// Written by Pat Thompson.
#ifndef _SIPPARSE_H_
#define _SIPPARSE_H_ 1
#include <string>
#include <Defines.h>
//#include <list>

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

// We use this for headers also.
struct SipParam {
	string mName, mValue;
	SipParam(string wName, string wValue) : mName(wName), mValue(wValue) {}
	SipParam() {}
};
struct SipParamList : public list<SipParam> {
	string paramFind(const char *name);
	string paramFind(string name) { return paramFind(name.c_str()); }
};

string parseURI(const char *buffer, SipParamList &params, SipParamList &headers);

// This is the subset of a full URI that we use.
// We dont care about URI params or headers.
// Note that the tag is a From or To generic-parameter, not a URI-parameter.
class SipUri : public string {		// The base class string contains the full URI.
	static const unsigned start = 4;		// strlen("sip:")
	public:
	string uriValue() const { return substr(); }	// return the whole thing.
	string uriUsername() const {	// username without the host or password.
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
	void setTag(string wTag) { mTag = wTag; rebuild(); }
	string getTag() const { return mTag; }
	string value() const { return mFullHeader; }
	string toFromValue() const { return mFullHeader; }
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

extern SipMessage *sipParseBuffer(const char *buffer);
extern void parseAuthenticate(string stuff, SipParamList &params);
extern void parseToParams(string stuff, SipParamList &params);
extern bool crackUri(string header, string *before, string *uri, string *tail);

}; // namespace
#endif
