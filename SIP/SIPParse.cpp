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
#define LOG_GROUP LogGroup::SIP		// Can set Log.Level.SIP for debugging
#include <string>
#include <list>
#include <string.h>
#include <Logger.h>
#include <stdlib.h>
#include <CodecSet.h>
#include <GSML3CCElements.h>
#include "SIPParse.h"
#include "SIPMessage.h"
#include "SIPBase.h"


namespace SIP {
using namespace std;

struct SipParseError : public std::exception {
	SipParseError() { LOG(DEBUG) << "SipParseError"; }
	virtual const char *what() const throw() {
		return "SipParseError";
	}
};


string makeUriWithTag(string username, string ip, string tag)
{
	return format("<sip:%s@%s>;tag=%s",username,ip,tag);
}
string makeUri(string username, string ip, unsigned port)
{
	if (port) {
		return format("sip:%s@%s:%u",username,ip,port);
	} else {
		return format("sip:%s@%s",username,ip);
	}
}


// endpos is the index of one char past the end of the string, eg, of the trailing nul. 
// Move endpos backward until we find a char that is not one of the trimchars, and leave
// endpos at the index one past that char, which is the appropriate length to substr everything but that char.
// Example: string result = input.substr(input.c_str(),input.size());
size_t trimrightn(const char *startp, size_t endpos, const char *trimchars /*=" \t\r\n"*/)
{
	while (endpos > 0 && strchr(trimchars,startp[endpos-1])) { endpos--; }
	return endpos;
}

// Return the index of the first char not in trimchars, or if none, of the trailing nul in the string.
// Note that if you trimleft and trimright a string of spaces, the two indicies would cross, so be careful.  See trimboth.
size_t trimleftn(const char *startp, size_t startpos/*=0*/, const char *trimchars /*=" \t\r\n"*/)
{
	while (startp[startpos] && strchr(trimchars,startp[startpos])) { startpos++; }
	return startpos;
}

// Trim both ends of a string.
string trimboth(string input, const char *trimchars /*=" \t\r\n"*/)
{
	size_t end = trimrightn(input.c_str(),input.size());
	size_t start = (end==0) ? 0 : trimleftn(input.c_str(),0,trimchars);
	return input.substr(start,end);
	//const char *bp = str.c_str();
	//const char *endp = str.c_str() + str.size(), *ep = endp;		// points to the trailing nul.
	//while (*bp && strchr(trimchars,*bp)) { bp++; }
	//while (ep > bp && strchr(trimchars,ep[-1])) { ep--; }
	//return (bp == str.c_str() && bp == endp) ? str : string(bp,bp-ep);
}

void commaListPushBack(string *cl, string val)
{
	val = trimboth(val," ,");	// Make sure there is no errant garbage around the new value.
	if (! cl->empty()) { cl->append(","); }
	cl->append(val);
}

void commaListPushFront(string *cl, string val)
{
	val = trimboth(val," ,");	// Make sure there is no errant garbage around the new value.
	if (cl->empty()) {
		*cl = val;
	} else {
		*cl = val + "," + *cl;
	}
}

string commaListPopFront(string *cl)	// Modify cl and return the result.
{
	string result;
	if (cl->empty()) { return result; }	// This is probably an error on the part of the caller.
	size_t cn = cl->find_first_of(',');
	if (cn == string::npos) {
		result = *cl;
		*cl = string("");
	} else {
		result = cl->substr(0,cn);
		*cl = trimboth(cl->substr(cn+1)," ,");	// The trimboth is paranoid overkill.
	}
	return trimboth(result," ,");	// Make extra sure there is no errant garbage around the result.
}

string commaListFront(string cl)
{
	size_t cn = cl.find_first_of(',');
	if (cn == string::npos) { cn = cl.size(); }
	return cl.substr(0,trimrightn(cl.c_str(),cn));
}


// Case insenstive string comparison
// Yes there is a way to do this in C++ by changing the character traits, but who cares.
bool strcaseeql(const char *a,const char *b) { return 0==strcasecmp((a),(b)); }
bool strncaseeql(const char *a,const char *b, unsigned n) { return 0==strncasecmp((a),(b),(n)); }
bool strceql(const string a, const string b) { return strcaseeql(a.c_str(),b.c_str()); }


//static const char *reserved = ";/?:@&=+$,";
//static const char *mark = "-_.!~*'()";
//static const char *param_unreserved = "[]/:&+$";
static const char *token = "-.!%*_+`'~"; 	// or alphanum

class SipChar {
	typedef unsigned char uchar;
	static char charClassData[256];
	enum {
		ccIsToken = 1
	};
	public:
	SipChar() {
		memset(charClassData,0,256);
		for (uchar ch = 'a'; ch <= 'z'; ch++) { charClassData[ch] = ccIsToken; }
		for (uchar ch = 'A'; ch <= 'Z'; ch++) { charClassData[ch] = ccIsToken; }
		for (uchar ch = '0'; ch <= '9'; ch++) { charClassData[ch] = ccIsToken; }
		for (uchar *cp = (uchar*)token; *cp; cp++) { charClassData[*cp] |= ccIsToken; }
	}
	static bool isToken(const uchar ch) { return charClassData[ch] & ccIsToken; }
	// unreserved is alphanum + mark.
} gSipChar;	// Without the global variable the class is never initialized.

char SipChar::charClassData[256];


// This is pretty easy to parse.  The major demarcation chars are reserved: @ ; ? &
// [sip: | sips: ]  user [: password] @ host [: port] [;param=value]* [? hname=havalue [& hname=hvalue]* ]
// absoluteURI ::= scheme: (hierpart | opaquepart)
// scheme ::= alphanum|[+-.]
// hierpart ::= // blah blah | / blah blah


// This can not distinguish between a missing param and one with an empty value.
SipParamList::iterator SipParamList::paramFindIt(const char *name)
{
	for (SipParamList::iterator it = this->begin(); it != this->end(); it++) {
		if (strceql(it->mName.c_str(),name)) { return it; }
	}
	return this->end();
}

string SipParamList::paramFind(const char*name)
{
	for (SipParamList::iterator it = this->begin(); it != this->end(); it++) {
		if (strceql(it->mName.c_str(),name)) { return it->mValue; }
	}
	return string("");
}


struct SipParseLine {
	const char *currentLine;	// Start of current line being parsed, used only for error messages.
	const char *pp;		// Pointer into the parse buffer.	It is const* for this class, but not for SipParseMessage

	void spLineInit(const char *buffer) { currentLine = pp = buffer; }
	SipParseLine(const char* buffer) { spLineInit(buffer); }
	SipParseLine() {}

	virtual void spError(const char *msg) {
		LOG(ERR) << "SIP Parse error "<<msg<<" in line:"<<currentLine;
		throw SipParseError();
	}

	void skipSpace()
	{
		while (*pp && isspace(*pp)) { pp++; }
	}

	bool scanChar(int ch)
	{
		skipSpace();
		if (*pp != ch) { return false; }
		pp++;
		skipSpace();
		return true;
	}

	// This is used for URIs, so no spaces allowed!
	string scanToSet(const char *sepchars) {
		const char *bp = pp;
		if ((pp = strpbrk(bp,sepchars)) == NULL) { pp = bp + strlen(bp); }
		return string(bp,pp-bp);
	}

	void scanUriParam(const char *sepchars, SipParamList &params) {
		SipParam param;
		const char *bp = pp;
		if ((pp = strpbrk(bp,sepchars)) == NULL) { pp = bp + strlen(bp); }
		if (const char *eqlp = (const char *)memchr(bp,'=',pp-bp)) {
			param.mName = string(bp,eqlp-bp);
			param.mValue = string(eqlp+1,pp-eqlp-1);
		} else {
			param.mName = string(bp,pp-bp);
		}
		if (! param.mName.empty()) params.push_back(param);
	}

	// Currently unsigned string of digits.
	int scanInt()
	{
		skipSpace();
		const char *bp = pp;
		while (*pp && isdigit(*pp)) pp++;
		if (bp == pp) { spError("expected integer"); }
		return atoi(bp);	// atoi ignores anything following the digits.
	}

	// Discard spaces, then return the next non-space string.
	// Leave pp pointing at the space char or end of string.
	string scanNonSpace()
	{
		skipSpace();
		if (!*pp) return string("");
		const char *bp = pp;
		while (*pp && ! isspace(*pp)) { pp++; }
		return string(bp,pp-bp);
	}

	// pp points at the quote starting the string.  Return string without the quotes.
	string scanQuotedString()
	{
		assert(*pp == '"');
		pp++;
		char *result = (char*)alloca(strlen(pp)), *rp = result;
		while (*pp) {
			if (*pp == '"') { pp++; return string(result,rp-result); }
			if (*pp == '\\') { pp++; }
			*rp++ = *pp++;
		}
		spError("unterminated quoted string");
		/*NOTREACHED*/
		return "";	// Never used.
	}

	string scanToken()
	{
		skipSpace();
		const char *bp = pp;
		//LOG(DEBUG) "char " << *pp <<"isToken="<<SipChar::isToken(*pp);
		while (SipChar::isToken(*pp)) { pp++; }
		return string(bp,pp-bp);
	}

	string scanTokenOrQuotedString()
	{
		return (*pp == '"') ?  scanQuotedString() : scanToken();
	}

	// The generic param is token = token or quoted string, with optional space around the '='.
	// The name is defined as a token, but this is not significant in the grammar; name is terminated by space or '='.
	// pp may point to space, scan past that first.
	// Leave pp pointing at the char immediately after the param, which may be a space or separator.
	bool scanGenericParam(SipParam &param)
	{
		const char *start = pp;
		param.mName = scanToken();
		if (scanChar('=')) {
			param.mValue = scanTokenOrQuotedString();
			if (param.mName.empty()) {
				LOG(NOTICE) << "empty parameter ignored in:"<<currentLine;
				return false;
			}
		} else {
			param.mValue.clear();
		}
		// not needed: skipSpace();
		LOG(DEBUG)<< LOGVAR(param.mName)<<LOGVAR(param.mValue)<<LOGVAR(start);
		return ! param.mName.empty();
	}

	// The URI may have almost any chars except it may not have embedded space.
	string parseLineURI(SipParamList &params, SipParamList &headers)
	{
		//params.clear(); headers.clear();	// make sure

		if (strncaseeql(pp,"sip:",4)) {
			pp += 4;
		} else if (strncaseeql(pp,"sips:",5)) {
			pp += 5;
		} else {
			LOG(ERR) << "Unrecognized URI scheme (not sip or sips):"<<pp;
			return "";
		}

		string address = scanToSet(";?> \t\f");
		while (*pp == ';') {
			pp++;
			scanUriParam(";?> \t\f",params);
		}
		while (*pp == '?' || *pp == '&') {
			pp++;
			scanUriParam("&> \t\f",headers);
		}
		return address;
	}
};

// We are not currently using this because we dont care about them.
// The uri-parameters appear within the <uri> and are: transport,user,moethod,ttl,lr.
// The headers appear after '&' with the <uri>
// In To: and From: there are generic-parameters after the URI outside the <>, including tag.
string parseURI(const char *buffer, SipParamList &uriparams, SipParamList &headers)
{
	try {
		SipParseLine parser(buffer);
		return parser.parseLineURI(uriparams,headers);
	} catch (...) {
		LOG(DEBUG) << "Caught SIP Parse error";
		return "";
	}
}

// Extract a SIP parameter from a string containing a list of parameters.  They look like ;param1=value;param2=value ...
// The input string need not start exactly at the beginning of the list.
// The paramid is specified with the semi-colon and =, example: ";tag="
static string extractParam(const char *pp, const char *paramid)
{
	const int taghdrlen = strlen(paramid);	// strlen(";tag=");
	if (const char *tp = strstr(pp,paramid)) {
		pp += taghdrlen;
		const char *ep = strchr(pp,';');
		return ep ? string(pp,ep-tp) : string(pp);
	}
	return string("");	// Not found.
}

void SipPreposition::rebuild()
{
	if (mTag.empty()) {
		mFullHeader = format("%s <%s>",mDisplayName,mUri.uriValue());
	} else {
		mFullHeader = format("%s <%s>;tag=%s",mDisplayName,mUri.uriValue(),mTag);
	}
	LOG(DEBUG) <<LOGVAR(mDisplayName) <<LOGVAR(mUri.uriValue()) <<LOGVAR(mFullHeader);
}

// Crack a URI or header into component parts:  before <uri>tail
// The pointers can be NULL.
// Return false if it is invalid.
bool crackUri(string header, string *before, string *uri, string *tail)
{
	//string junk;
	//int n = myscanf("%s <%[^>]>%s",before?before:&junk,uri?uri:&junk,tail?tail:&junk);
	size_t nUriBegin = header.find_first_of('<');
	if (nUriBegin == string::npos) {
		// Old format allows the URI without <> but it is not possible to specify the tag parameter that way.
		// It should begin with "sip:" or "sips:" but we are not checking.
		if (uri) *uri = header;
		return true;
	}
	size_t nUriEnd = header.find_first_of('>',nUriBegin);
	if (nUriBegin == string::npos) { return false; }
	// Warning: The display name may be a quoted string or multiple unquoted tokens.
	if (before) *before = header.substr(0,trimrightn(header.c_str(),nUriBegin));
	if (uri) *uri = header.substr(nUriBegin+1,nUriEnd-nUriBegin-1);
	if (tail) *tail = header.substr(nUriEnd+1);
	return true;	// happiness
}

// Parse immediately, only we're not going to do a full parse on this either.  All we care about is the tag.
// Note that the tag param is outside the <uri>
void SipPreposition::prepSet(const string header)
{
	mFullHeader = header;
	mDisplayName.clear();
	mTag.clear();
	mUri.clear();
	if (header.empty()) { return; }
	try {
		if (1) {
			// We dont need the parser for this.  It is trivial.
			string uri, tail;
			if (!crackUri(header,&mDisplayName,&uri,&tail)) {
				LOG(ERR) << "Bad SIP Contact field:"<<header;
				return;
			}
			mUri.uriSet(uri);
			mTag = extractParam(tail.c_str(),";tag=");
		} else {
			// This code works fine, its just overkill.
			SipParseLine parser(header.c_str());
			string thing = parser.scanNonSpace();
			parser.skipSpace();
			if (*parser.pp == '<') {
				mDisplayName = thing;	// Warning: this may or may not have quotes.
				const char *ep = strchr(parser.pp,'>');
				if (!ep) {
					LOG(ERR) << "Bad SIP Contact field:"<<header;
					ep = parser.pp + strlen(parser.pp);	// guess
				}
				mUri.uriSet(string(parser.pp+1,ep-parser.pp-1));	// SipUri strips the < > off the ends of the URI.
				parser.pp = ep;
				mTag = extractParam(parser.pp,";tag=");
			} else {
				mUri.uriSet(thing);
			}
		}
	} catch (...) {
		LOG(DEBUG) << "Caught SIP Parse error";
	}
}

void parseToParams(string stuff, SipParamList &params)
{
	SipParseLine parser(stuff.c_str());
	try {
		do  {
			SipParam param;
			if (! parser.scanGenericParam(param)) break;
			params.push_back(param);
		} while (parser.scanChar(','));
	} catch(SipParseError) {
		// error was already logged.
		LOG(DEBUG) << "Caught SIP Parse error";
		return;
	}
}

// This is only for www-authenticate, not for Authentication-Info
void parseAuthenticate(string stuff, SipParamList &params)
{
	SipParseLine parser(stuff.c_str());

	try {
		string challengeType = parser.scanToken();
		if (! strceql(challengeType,"Digest")) {
			LOG(ERR) << format("unrecognized challenge type:%s",challengeType.c_str());
			return;
		}

		do  {
			SipParam param;
			// (pat 9-2014) sipauthserve incorrrectly puts extra commas in the Authorization string,
			// and this would reject it.  This has not been a problem because we dont use that field, but lets fix it anyway.
			// formerly: if (! parser.scanGenericParam(param)) break;
			if (parser.scanGenericParam(param)) {
				params.push_back(param);
			}
		} while (parser.scanChar(','));
	} catch(SipParseError) {
		// error was already logged.
		LOG(DEBUG) << "Caught SIP Parse error";
		return;
	}
}

// You can pass in the comma-separated list of vias and it will parse just the first.
void SipVia::viaParse(string vialine)
{
	LOG(DEBUG) <<LOGVAR(vialine);
	this->assign(vialine);
	// Spaces are allowed anywhere in the via spec even though most people dont insert htem.
	// Example: "SIP / 2.0 / UDP host : port ; branch = branchstring"
	// The port may be empty. There may be options after the port.
	try {
		SipParseLine parser(vialine.c_str());
		parser.scanToken();	// protocol-name
		parser.scanChar('/');
		parser.scanToken();	// protocol-version
		parser.scanChar('/');
		parser.scanToken();	// transport
		mSentBy = parser.scanToken();	// host
		if (parser.scanChar(':')) {
			mSentBy.append(":");
			mSentBy.append(parser.scanToken());		// port
		}
		// Now the list of via-params
		//SipParam param;
		//while (parser.scanGenericParam(param)) {
		//	if (strcaseeql(param.mName.c_str(),"branch")) {		// not sure if this is case insensitive, but be safe.
		//		mViaBranch = param.mValue;
		//		break;	// We can break; we dont care about any other parameters.
		//	}
		//}
		while (parser.scanChar(';')) {
			SipParam param;
			while (parser.scanGenericParam(param)) {
				if (strcaseeql(param.mName.c_str(),"branch")) {     // not sure if this is case insensitive, but be safe.
					mViaBranch = param.mValue;
					break;  // We can break; we dont care about any other parameters.
				}
			}
		}
	} catch(...) {
		LOG(ERR) << "Error parsing via:"<<vialine;
	}

	// Another way I didnt use:
	//int n = myscanf("%[^/ \t] / %[^/ \t] / %s %[^:; \t] : %[^; \t]",
		//mProtocolName, mProtocolVersion, mTransport, mHost, mPort);
}

class SipParseMessage : SipParseLine {
	char *start;	// Start of the parse buffer.
	char *eolp;		// Pointer to end of current line.
	unsigned linecnt;

	void spInit(char *buffer) {
		start = buffer;
		linecnt = 0;
		eolp = NULL;
		spLineInit(buffer);
	}

	void logError(const char *msg) {
		LOG(ERR) << "SIP Parse error at line "<<linecnt<<" "<<msg<<". Parsing:'"<<pp<<"'"<< " SIP message="<<start;
	}

	void spError(const char *msg) {
		if (eolp) { *eolp = '\r'; }		// Make the buffer printable again.
		logError(msg);
		throw SipParseError();
	}

	// Return true if there is another line, and nul terminate it; false when the header is complete.
	// Buffer is modified in place.  Line continuations are removed by substituting spaces for the CR,NL.
	bool nextLine()
	{
		if (eolp) {
			// Put the CR back so the message is printable in error messages.
			*eolp = '\r'; 		// Put the CR back in the last line so the message is printable in error messages.
			spLineInit(eolp+2);	// sets pp to start of next line.
		}
		eolp = Unconst(pp);
		while ((eolp = strstr(eolp,"\r\n"))) {
			LOG(DEBUG) <<LOGVAR((void*)start)<<LOGVAR((void*)pp)<<LOGVAR((void*)eolp);
			linecnt++;
			if (eolp == pp) { return false; }		// Found the terminating blank line.
			if (eolp[2] == ' ' || eolp[2] == '\t') { *eolp++ = ' '; *eolp++ = ' '; continue; }	// Combine continuation lines.
			*eolp = 0;								// Terminate.
			return true;
		}
		spError("unexpected end of message");
		/*NOTREACHED*/
		return "";	// Never used.
	}

	void scanSipVersion()
	{
		if (!  (strncaseeql(pp,"SIP",3))) { spError("Expecting 'SIP'"); }
		pp += 3;	// skip over 'SIP'
		if (*pp++ != '/') { spError("Invalid SIP-version"); }
		string version = scanNonSpace(); // Discard the version number.  We dont really care what it is.
		// We hope it will be "2.0/UDP" or "2.0/TCP";
		if (strncmp(version.c_str(),"2.0",3)) { LOG(NOTICE) << "unexpected SIP version="<<version; }
	}

	// Buffer is modified in place - lines are terminated at eols.
	public:
	SipMessage * sipParse()
	{
		//LOG(DEBUG);
		SipMessage *sipmsg = new SipMessage();
		// Scan the first line.
		if (!nextLine()) { spError("Empty SIP message"); }
		//LOG(DEBUG);
		// The message is either a request or a response.
		// Response Status-Line     =  SIP-Version SP Status-Code SP Reason-Phrase CRLF
		// SIP-Version    =  "SIP" "/" 1*DIGIT "." 1*DIGIT
		// Request Request-Line = Method SP Request-URI SP SIP-Version CRLF
		if (strncaseeql(pp,"SIP",3)) {
			// This is a response.
			scanSipVersion();
			sipmsg->msmCode = scanInt();
			skipSpace();
			sipmsg->msmReason = string(pp);	// Rest of the line is the reason.
		} else {
			// This is a request.
			sipmsg->msmReqMethod = scanNonSpace();
			sipmsg->msmReqUri = scanNonSpace();
			skipSpace();
			scanSipVersion();
		}
		skipSpace();
		//LOG(DEBUG);

		// Get the rest of the header lines.
		while (nextLine()) {
			// We have a header line.  The headers names themselves are case insensitive.
			LOG(DEBUG) << "nextLine="<<pp;
			string name = scanToken();
			LOG(DEBUG) << LOGVAR(name);
			if (name.empty() || !scanChar(':')) { spError("Line without header"); }

			if (strceql(name,"to") || strceql(name,"t")) {
				sipmsg->msmTo.prepSet(string(pp));
			} else if (strceql(name,"from") || strceql(name,"f")) {
				sipmsg->msmFrom.prepSet(string(pp));
			} else if (strceql(name,"contact") || strceql(name,"m")) {
				sipmsg->msmContactValue = string(pp);
			} else if (strceql(name,"CSeq")) {
				sipmsg->msmCSeqNum = scanInt();
				sipmsg->msmCSeqMethod = scanToken();
			} else if (strceql(name,"call-id") || strceql(name,"i")) {
				// The call-id string is defined as word[@word], but unless we are really interested
				// in validating incoming SIP messages, we can simply scan for non-space.
				sipmsg->msmCallId = scanNonSpace();
			} else if (strceql(name,"via") || strceql(name,"v")) {
				// Multiple vias can appear on separate lines or be comma separated in one line,
				// so for simplicity we will just keep them all in a comma separated list.
				commaListPushBack(&sipmsg->msmVias,pp);
			} else if (strceql(name,"record-route")) {
				commaListPushBack(&sipmsg->msmRecordRoutes,pp);
			} else if (strceql(name,"route")) {
				commaListPushBack(&sipmsg->msmRoutes,pp);
			} else if (strceql(name,"max-forwards")) {
				sipmsg->msmMaxForwards = trimboth(string(pp));	// We already trimmed left, but its ok to do it again.
			// No need to treat this specially here, even though authenticate info does not follow normal SIP parsing rules.
			//} else if (strceql(name,"www-authenticate")) {
			//	// authenticate info does not follow normal SIP parsing rules.
			//	sipmsg->msmAuthenticateValue = string(pp);
			} else if (strceql(name,"content-type")) {
				sipmsg->msmContentType = string(pp);
			} else if (strceql(name,"reason")) {
				sipmsg->msmReasonHeader = string(pp);
			} else {
				SipParam param(name,string(pp));
				sipmsg->msmHeaders.push_back(param);
			}
		}
		LOG(DEBUG) << "end";
		if (pp[0] == '\r' && pp[1] == '\n') {
			pp += 2;
			sipmsg->msmBody = string(pp);
		} else {
			// Dont abort for this.  Just log an error.
			logError("Missing message body");
		}
		return sipmsg;
	}

	SipParseMessage(char *buffer) { spInit(buffer); }
};

SipMessage *sipParseBuffer(const char *buffer)
{
	LOG(DEBUG) << "DEBUG";
	// The SIP Parser modifies buffer, but puts it back the way it found it.
	// This is OK because no thread contention for the buffer is possible because all callers pass a unique string for parsing.
	SipParseMessage parser(Unconst(buffer));
	SipMessage *result;
	try {
		result = parser.sipParse();
	} catch (SipParseError) {
		// error was already logged.
		LOG(DEBUG) << "SipParseError caught";
		return NULL;
	}
	return result;
}

void codecsToSdp(Control::CodecSet codecs, string *codeclist, string *attrs)
{
	attrs->clear();
	attrs->reserve(80);
	codeclist->reserve(20);
	// We are using the same code for offers and answers, so for now only included the one we want:
	/**
	if (codecs.isSet(PCMULAW)) {
		attrs->append("a=rtpmap:0 PCMU/8000\r\n");
		if (!codeclist->empty()) { codeclist->append(" "); }
		codeclist->append("0");
	}
	***/
	if (codecs.isSet(Control::GSM_FR) || codecs.isSet(Control::GSM_HR) || codecs.isEmpty()) {
		attrs->append("a=rtpmap:3 GSM/8000\r\n");
		if (!codeclist->empty()) { codeclist->append(" "); }
		codeclist->append("3");
	}
	// TODO: Does the half-rate codec need a special RTP format is conversion performed lower down?
	// RFC5993 7.1 says it is "a=rtpmap:<dynamic-port-number. GSM-HR-08/8000"
	/**
	if (codecs.isSet(Control::AMR_FR) || codecs.isSet(Control::AMR_HR)) {
		// Dynamically allocated SDP starts at 96.
		attrs->append("a=rtpmap:96 AMR/8000\r\n");
		if (!codeclist->empty()) { codeclist->append(" "); }
		codeclist->append("96");
	}
	**/
}


// We dont fully parse it; just pull out the o,m,c,a lines.
void SdpInfo::sdpParse(const char *buffer)
{
	const char *bp, *eol;
	for (bp = buffer; bp && *bp; bp = eol ? eol+1 : NULL) {
		eol = strchr(bp,'\n');
		switch (*bp) {
			case 'o':
				if (myscanf(bp,"o=%s %s %s IN IP4 %s",&sdpUsername,&sdpSessionId, &sdpVersionId, &sdpHost) < 4) {
					LOG(ERR) << "SDP unrecognized o= line, sdp:"<<buffer;
				}
				break;
			case 'm': {
				string portTmp;
				if (myscanf(bp,"m=%*s %s %*s %s",&portTmp, &sdpCodecList) < 2) {
					LOG(ERR) << "SDP unrecognized m= line, sdp:"<<buffer;
				}
				sdpRtpPort = atoi(portTmp.c_str());
				break;
				}
			case 'c':
				if (myscanf(bp,"c=IN IP4 %s",&sdpHost) < 1) {
					// If we were paranoid we could check if it matches the o= line.
					LOG(ERR) << "SDP unrecognized c= line, sdp:"<<buffer;
				}
				break;
			case 'a':
				// It would crash if eol were null because SDP was truncated, so check.
				if (eol) { sdpAttrs += string(bp,eol-bp+1); }
				break;
		}
	}
}

void SdpInfo::sdpInitOffer(const SipBase *dialog)
{
	sdpUsername = dialog->sipLocalUsername();
	sdpRtpPort = dialog->vGetRtpPort();
	sdpHost = dialog->localIP();
	codecsToSdp(dialog->vGetCodecs(),&sdpCodecList,&sdpAttrs);
	static const string zero("0");
	sdpSessionId = sdpVersionId = zero;
}

void SdpInfo::sdpInitRefer(const SipBase *dialog, int remotePort)
{
	sdpInitOffer(dialog);
	// Same as above, except:
	sdpRtpPort = remotePort;
	sdpVersionId = format("%lu",time(NULL));
}

// Note that sdp is not completely order independent.
string SdpInfo::sdpValue()
{
	string result; result.reserve(100);
	char buf[302];
	result.append("v=0\r\n");		// SDP protocol version
	// originator, session id, ip address.
	snprintf(buf,300,"o=%s %s %s IN IP4 %s\r\n",sdpUsername.c_str(),sdpSessionId.c_str(),sdpVersionId.c_str(),sdpHost.c_str());
	result.append(buf);
	// RFC3264 5: And I quote:
	//	"The SDP 's=' line conveys the subject of the session, which is reasonably defined for multicast,
	// but ill defined for unicast.  For unicast sessions, it is RECOMMENDED that it consist of a single space
	// character (0x20) or a dash (-)."
	// I dont know why we are setting it to 'Talk Time'.
	result.append("s=Talk Time\r\n");
	result.append("t=0 0\r\n");		// time session is active; 0 means unbounded.
	snprintf(buf,300,"m=audio %u RTP/AVP %s\r\n",sdpRtpPort,sdpCodecList.c_str());	// media name and transport address.
	result.append(buf);	// media name and transport address.
	// Optional connection information.  Redundant because we included in 'o='.
	snprintf(buf,300,"c=IN IP4 %s\r\n",sdpHost.c_str());
	result.append(buf);
	result.append(sdpAttrs);
	return result;
}


// See L3Cause in L3Enums.h
string CallTerminationCause::getQ850CallTermText(int l3Cause) {
	switch ((GSM::L3Cause::CCCause) l3Cause) {
		case GSM::L3Cause::Unknown_L3_Cause:  return "Unknown Cause 0";
		case GSM::L3Cause::Unassigned_Number:  return "Unallocated Number"; // 1
		case GSM::L3Cause::No_Route_To_Destination:  return "No Route to Destination";
		case GSM::L3Cause::Channel_Unacceptable:  return "Channel Unacceptable";
		case GSM::L3Cause::Operator_Determined_Barring:  return "Preemption";
		case GSM::L3Cause::Normal_Call_Clearing:  return "Normal Call Clearing"; // 16
		case GSM::L3Cause::User_Busy:  return "User Busy"; // 17
		case GSM::L3Cause::No_User_Responding:  return "No User Responding"; // 18
		case GSM::L3Cause::User_Alerting_No_Answer:  return "No Answer from User (User alerted)";
		case GSM::L3Cause::Call_Rejected:  return "Call Rejected"; // 21
		case GSM::L3Cause::Number_Changed:  return "Number Changed";
		case GSM::L3Cause::Preemption:  return "Exchange Routing Error";
		case GSM::L3Cause::Non_Selected_User_Clearing:  return "Non-selected User Clearing";
		case GSM::L3Cause::Destination_Out_Of_Order:  return "Destination Out of Order";
		case GSM::L3Cause::Invalid_Number_Format:  return "Invalid Number Format (Address incomplete)";
		case GSM::L3Cause::Facility_Rejected:  return "Facility Rejected";
		case GSM::L3Cause::Response_To_STATUS_ENQUIRY:  return "Response to STATUS ENQUIRY";
		case GSM::L3Cause::Normal_Unspecified:  return "Normal, Unspecified";
		case GSM::L3Cause::No_Channel_Available:  return "No Circuit/Channel Available";
		case GSM::L3Cause::Network_Out_Of_Order:  return "Network Out of Order";
		case GSM::L3Cause::Temporary_Failure:  return "Temporary Failure";
		case GSM::L3Cause::Switching_Equipment_Congestion:  return "Switching Equipment Congestion";
		case GSM::L3Cause::Access_Information_Discarded:  return "Access Information Discarded";
		case GSM::L3Cause::Requested_Channel_Not_Available:  return "Requested Circuit/Channel N/A";
		case GSM::L3Cause::Resources_Unavailable:  return "Resource Unavailable, Unspecified";
		case GSM::L3Cause::Quality_Of_Service_Unavailable:  return "Quality of Service Not Available";
		case GSM::L3Cause::Requested_Facility_Not_Subscribed:  return "Requested Facility Not Subscribed";
		case GSM::L3Cause::Incoming_Calls_Barred_Within_CUG:  return "Outgoing Calls Barred Within CUG";
		case GSM::L3Cause::Bearer_Capability_Not_Authorized:  return "Incoming Calls Barred Within CUG";
		case GSM::L3Cause::Bearer_Capability_Not_Presently_Available:  return "Bearer Capability Not Available";
		case GSM::L3Cause::Service_Or_Option_Not_Available:  return "Service or Option N/A, unspecified";
		case GSM::L3Cause::Bearer_Service_Not_Implemented:  return "Bearer Capability Not Implemented";
		case GSM::L3Cause::ACM_GE_Max:  return "ACM greater or equal to ACM max";
		case GSM::L3Cause::Requested_Facility_Not_Implemented:  return "Requested Facility Not Implemented";
		case GSM::L3Cause::Only_Restricted_Digital_Information_Bearer_Capability_Is_Available:  return "Only Restricted Digital Bearer Cap supported";
		case GSM::L3Cause::Service_Or_Option_Not_Implemented:  return "Service or Option Not Implemented, Unspecified";
		case GSM::L3Cause::Invalid_Transaction_Identifier_Value:  return "Invalid Call Reference Value";
		case GSM::L3Cause::User_Not_Member_Of_CUG:  return "User Not Member of CUG";
		case GSM::L3Cause::Incompatible_Destination:  return "Incompatible Destination";
		case GSM::L3Cause::Invalid_Transit_Network_Selection:  return "Invalid Transit Network Selection";
		case GSM::L3Cause::Semantically_Incorrect_Message:  return "Invalid Message, Unspecified";
		case GSM::L3Cause::Invalid_Mandatory_Information:  return "Mandatory Information Element is Missing";
		case GSM::L3Cause::Message_Type_Not_Implemented:  return "Message Type Non-existent / Not Implemented";
		case GSM::L3Cause::Messagetype_Not_Compatible_With_Protocol_State:  return "Message Incompatible With Call State or Message Type";
		case GSM::L3Cause::IE_Not_Implemented:  return "IE/Parameter Non-existent or Not Implemented";
		case GSM::L3Cause::Conditional_IE_Error:  return "Invalid Information Element Contents";
		case GSM::L3Cause::Message_Not_Compatible_With_Protocol_State:  return "Message Not Compatible With Call State";
		case GSM::L3Cause::Recovery_On_Timer_Expiry:  return "Recovery on Timer Expiry";
		case GSM::L3Cause::Protocol_Error_Unspecified:  return "Message With Unrecognized Parameter, Discarded";
		case GSM::L3Cause::Interworking_Unspecified:  return "Interworking, Unspecified";
		default:  return "";
	} // switch

	return "";
} // getQ850CallTermText


string CallTerminationCause::getSIPCallTermText(int iSIPError) {
	// Fill in text as needed
	if  (iSIPError != 0)
		return "SIP ERROR";
	else
		return "";
}


};	// namespace SIP
