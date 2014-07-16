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

// Written by Pat Thompson

#define LOG_GROUP LogGroup::Control

#include <string>
#include <list>
#include "L3SupServ.h"
#include "L3TranEntry.h"
#include "L3MMLayer.h"
#include <GSMCommon.h>
#include <GSML3Message.h>
#include <GSML3SSMessages.h>
#include <GSMLogicalChannel.h>
#include <SIPDialog.h>
#include <Globals.h>


// Generic SS messages are transferred by the Facility IE, whose content is described by 24.080 4.61
// and by an ASN description in the 24.080 appendix.  This encoding is actually part of the GSM MAP reference
// lifted out of ITU Q.773.  The MAP messages are in 3GPP 29.002 11.9 through 11.11.
// The Facility IE may appear in the 'call-related' messages Alerting, Call Proceeding, Setup, etc. described in 24.008,
// or in 'call-independent' L3 messages described in 24.080: Facility, Register, Release Complete.
// However, the unstructured USupServ operations may only appear in call-independent messages, except that phase 1 MS
// may send them in call-dependent locations, so we better just recognize them anywhere.

// I am not going to suck in the ASN description, rather just pull out the tiny pieces we need.
// The USupServ version 1 command is processUnstructuredSS-Data (3GPP 4.80) and has one argument: (ss-UserData)
// The USupServ version 2 command is processUnstructuredSS-Request (3GPP 24.80) and has two arguments: (uSupServ-DataCodingScheme, uSupServ-String)

// It was difficult to find the operation codes.  The ASN referenced in 24.080 is not up to date, but 4.80 is!
// Then you have to look in 24.090 to find out what these names mean.
// ProcessUnstructuredSS-Data ::= localValue 19		// USupServ version 1 MS->network request.
// ProcessUnstructuredSS-Request ::= localValue 59	// USupServ version 2 MS->network request.
// UnstructuredSS-Request ::= localValue 60			// USupServ version 2 network->MS request.
// UnstructuredSS-Notify ::= localValue 61			// USupServ version 2 network->MS notification.

// 22.030 6.5.2: man-machine interface (MMI) for USupServ.
// These are the key presses:  SC = Service Code (2 or 3 digits) SI = Supplementary Information
// Activation: *SC*SI#
// Deactivation: #SC*SI#
// Interrogation: *#SC*SI#
// Registration: *SC*SI# and **SC*SI#
// Erasure: ##SC*SI#
// UE Determines whether single * is activation or registration,
// "For example, a call forwarding request with a single * would be interpreted as registration if containing
//			a forwarded-to number, or an activation if not."
// The *SI may be absent, or *SIA*SIB*SIC (followed by #).
// In the above, SIA or SIB or SIC may be absent, for example *SC***SIC#

// The service codes are in GSM 02.30 annex B.
// The SS-operation-codes are in from http://cpansearch.perl.org/src/REHSACK/Net-Radio-Location-SUPL-Test-0.001/asn1src/MAP-SS-Code.asc
// Here is a register message:    0b7b1c0da10b0201010201 0c 30 03 04 01 91                      7f 01 00
// 0b	// PD=0xb.
// 7b MTI (0x3b) ored with 0x80.
// 1c FacilityIEI
// 0d  length of facility contents == 13
// 		a1		component type tag = Invoke (24.080 3.6.2)
// 		0b		component length == 12
//		02		Component id tag == InvokeID
//		01		invoke id length
//		01		invoke id
//		Optional LinkedID tag 0x80 not present.
//		02		OperationCodeTag
//		01		OperationCode length
//		0c		operation code 12 == activateSS.
//			parameter list is from 4.80 4.5 ASN: activateSS OPERATION ARGUMENT
//			I thought I typed *34#, which came out like this:
//			ss codes from http://cpansearch.perl.org/src/REHSACK/Net-Radio-Location-SUPL-Test-0.001/asn1src/MAP-SS-Code.asn
//			or from 3GPP 9.02 7.6.4 page 268.
//			3GPP 9.02 says: 0x30 is "allCallOfferingSS" in decimal == 48.
//		30		Sequence tag.
//		03		length of sequence
//		04		- octet?
//		01		length of param
//		91		the activateSS argument data, maybe "BarringOfOutgoingCalls SS-Code == 91.  Selected by *33.
// 7f  version indicator IEI
// 01  24.080 3.7.2: value 1 indicates SS-Protocol version 3 and phase 2 error handling.
// 00 what is this?  Maybe it has to be word aligned?
// -------------
// Here is another, I typed *78#: 1b7b1c13a1110201010201 3b 30 09 04 01 0f 04 04 aa 1b 6e 04    7f 01 00
//		This time: operation code 0x3b == processUnstructuredSSRequest
//		30 Sequence Tag
//		09 sequence length (guessing)
//		04 - octet?
//		01 length of param
//		0f == CBS Data Coding scheme language unspecified. (23.038 5)
//		04 - octet?
//		04 length of param
//		aa1b6e04 is 23.038 6.1.2.3 UUSD packing of "*78#"

// Here is #78#:
// 0b7b1c13a11102010002013b300904010f0404a31b6e047f0100
// ...
// 30  sequence tag
// 09 seq len
// 04 first param is octet
// 01 first param len
// 0f
// 04 second param it octet
// 04 second param len
// a31b6e04
// 7f0100  what is this?

// =======
// This is what the blackberry sent after being sent a Facility message:
// rawdata=a203020100
// 	a2 is ReturnResult
//	03 length
//	02 Component ID = invokeId
//	01 length
//	00 invokeID was 0.
// and no args, so I guess not much result, huh.


namespace Control {
using namespace SIP;

struct SSParseExcept {
	string mErrorMessage;
	SSParseExcept(string message) : mErrorMessage(message) {}
};

// From GSM 4.80 Annex A.
enum SSOperationCode {
	registerSS = 10,
	eraseSS = 11,
	activateSS = 12,
	deactivateSS = 13,
	interrogateSS = 14,
	notifySS = 16,
	registerPassword = 17,
	getPassword = 18,
	processUnstructuredSSData = 19,		// 0x13 USSD version 1 MS->network request.
	forwardCheckSSIndication = 38,
	processUnstructuredSSRequest = 59,	// 0x3b USSD version 2 MS->network request.
	unstructuredSSRequest = 60,
	unstructuredSSNotify = 61,			// version 2 notification.
	eraseCCEntry = 77,
	callDeflection = 117,
	userUserService = 118,
	accessRegisterCCEntry = 119,
	forwardCUGInfo = 120,
	splitMPTY = 121,
	retrieveMPTY = 122,
	holdMPTY = 123,
	buildMPTY = 124,
	forwardChargeAdvice = 125,
	explicitCT = 126,
	lcsLocationNotification = 116,
	lcsMOLR = 115
};

// Decode the unstructured data string as per 23.038, 7bit chars to 8bit chars.
static string ussdDecode(string in)
{
	LOG(DEBUG) << LOGVAR(in.size()) <<" " <<data2hex(in.data(),in.size());
	// The maximum size is very limited so we can use a presized buffer.
	unsigned char result[250], *rp = result;
	unsigned offset = 0;
	unsigned accum = 0;
	const unsigned char *indata = (const unsigned char*)in.data();
	unsigned indatalen = in.size();
	for (unsigned i = 0; i < indatalen; i++) {
		accum = accum | (indata[i] << offset);
		//LOG(DEBUG) <<LOGHEX(i)<<LOGHEX(offset)<<LOGHEX(accum)<<LOGHEX(indata[i]);
		*rp++ = decodeGSMChar(accum & 0x7f);
		accum >>= 7;
		// When we have accumulated two 7-bit characters, we output them both.
		if (++offset == 7) {
			*rp++ = decodeGSMChar(accum & 0x7f);
			accum >>= 7;
			offset = 0;
		}
	}
	// Special goofy case as per 23.038 6.1.2.3 - If there are exactly 7 bits at the end they are padded with a CR,
	// which we must remove.  Update: I dont think we are required to remove the extra CR, could just leave it.
	int len = rp - result;
	if (len && len % 8 == 0 && result[len-1] == '\r') { len--; }
	return string((char*)result,len);
};

// Encode the string as per 23.038, packing 8bit chars into 7bit chars packed tightly.
static string ussdEncode(string in)
{
	unsigned char result[280], *rp = result;
	const unsigned char *indata = (const unsigned char*)in.data();
	unsigned indatalen = in.size();
	unsigned offset = 0;
	unsigned accum = 0;
	for (unsigned i = 0; i < indatalen; i++) {
		accum = accum | (encodeGSMChar(indata[i] & 0x7f) << offset);
		if (offset == 0) {
			offset = 7;
		} else {
			*rp++ = accum & 0xff;
			accum >>= 8;
			offset--;
		}
	}
	if (offset == 1) {
		// Special case as per 23.038 6.1.2.3 - If there are exactly 7 bits at the end they are padded with a CR.
		accum |= ('\r' << 1);
	}
	if (offset) {
		*rp++ = accum & 0xff;
	}
	return string((char*)result,rp-result);
}


// This class is just a wrapper to conveniently contain the ssCodes map.
class SupServCodes
{
	map<unsigned,string> ssCodes;

	void addSSCode(unsigned ssCode,string serviceCodeDigits)
	{
		ssCodes[ssCode] = serviceCodeDigits;
	}


	void ssCodesInit()
	{
		// the mapId is a MAP-SS-Code 22.030 Annex B and
		// from http://cpansearch.perl.org/src/REHSACK/Net-Radio-Location-SUPL-Test-0.001/asn1src/MAP-SS-Code.asn
		addSSCode(0xa1,"75");	// eMLPP.  22.067  Also 75n n=0..4  ehanced Multilevel Precedence Pre-emption service.
		addSSCode(0x24,"66"); // CD.  Call Deflection 22.072 
		addSSCode(0x11, "30");		// CLIP  22.081  Calling Line Identification Presentation
		addSSCode(0x12, "31");		// CLIR  Calling Line Identification Restriction
		addSSCode(0x13, "76");		// COLP  Connected Line Identification Presentation
		addSSCode(0x14, "77");		// COLR  Connected Line Identification Restriction
		// In temporary mode, to suppress CLIR for a single call, enter: ' * 31 # <called number> SEND '
		// In temporary mode, to invoke CLIR for a single call enter: ' # 31 # <called number> SEND '
		addSSCode(0x41, "21");		// CFU 22.082
		addSSCode(0x29, "67");		// CFB call forward Busy
		addSSCode(0x2a, "61");		// cfnry CF No Reply
		addSSCode(0x2b,"62");		// cfnrc CF Not Reachable
		addSSCode(0x20,"002");		// all CallForwardingSS
		addSSCode(0x28,"004");		// all conditional CF
		addSSCode(0x41,"43");		// WAIT 22.083  Call waiting?
		// I think HOLD and MultiParty are handled by L3 messages at the MS level, and these MAP-SS-Codes are used only at the MSC level.
		//addSSCode(0x42,??); // HOLD see section 4.5.5.  Ha ha, there is no such section in either 22.030 or 22.083.  It is in 2.30.
		//addSSCode(0x51, ??); // MPTTY see 4.5.5 22.084
		addSSCode(0x81,"361"); // UUS Service 1 22.087
		addSSCode(0x82,"362"); // UUS Service 2 22.087
		addSSCode(0x83,"363"); // UUS Service 3 22.087
		addSSCode(0x80, "360"); // all UUS Services.	allAdiitionalInfoTransferSS reserved for future use?

		// SS-Codes 0x1a to 0x1f are reserved for future use.


		addSSCode(0x90, "330");	// all Barring Serv.
		addSSCode(0x91, "??");	// barringOfOutgoingCalls.  This has a separate code other than baoc.
		addSSCode(0x92, "33");	// BAOC 22.088	barring of outgoing calls.
		addSSCode(0x93,"331");	// BAOIC	barring of outgoing international calls.
		addSSCode(0x94, "332");	// BAOIC exc home country
		addSSCode(0x9a, "35");	// BAIC barring of incoming calls
		addSSCode(0x9b, "351");	// BAIC roaming
		//addSSCode "333");	// Outg. Barr. Serv.
		//addSSCode "353");	// Inc. Barr. Serv.
		addSSCode(0x31,"96");	// ECT 22.091  Explicit Call Transfer
		addSSCode(0x43, "37");	// CCBS-A 22.093  Completion of Call to Busy Subscribers.
				// CCBS-A SS-code is used only in insert,delete,interrogate SS
		addSSCode(0x44, "37");	// CCBS-B This SS-code is used only in insert,delete,interrogate SS
		// addSSCode(??,"214");	// FM 22.094
		addSSCode(0x19, "300");	// CNAP 22.096
		//addSSCode(??, "591");	// MSP 22.097	also 592,593,594
		// 0x51 CUG closed user group
		addSSCode(0x45,"88");	// MC 22.135  Multicall
		// 22.030 Annex C
		addSSCode(0xb0,"50");
		addSSCode(0xb1,"51");
		addSSCode(0xb2,"52");
		addSSCode(0xb3,"53");
		addSSCode(0xb4,"54");
		addSSCode(0xb5,"55");
		addSSCode(0xb6,"56");
		addSSCode(0xb7,"57");
		addSSCode(0xb8,"58");
		addSSCode(0xb9,"59");
		addSSCode(0xba,"60");
		addSSCode(0xbb,"61");
		addSSCode(0xbc,"62");
		addSSCode(0xbd,"63");
		addSSCode(0xbe,"64");
		addSSCode(0xbf,"65");

		// aoci advice of charge information??
		// aocc advice of charge charging??
		// A bunch of location services.
	}

	public:
	string ssMapIdToServiceCodeDigits(unsigned mapId)
	{
		if (ssCodes.size() == 0) ssCodesInit();
		map<unsigned,string>::const_iterator foo = ssCodes.find(mapId);
		if (foo != ssCodes.end()) { return foo->second; }
		return format("unknownMapId(0x%x)",mapId);
	}
} supServCodes;	// The one and only instance of this class.

class SSMapCommand
{
	enum SSComponentTypeTag {
		ssInvokeTag = 0xa1,
		ssReturnResultTag = 0xa2,
		ssReturnErrorTag = 0xa3,
		ssRejectTag = 0xa4
	};

	enum SSComponentIdTag {
		ssInvokeIDTag = 0x02,
		ssLinkedIDTag = 0x80,
	};

	static const unsigned ssOperationCodeTag = 0x02;

	enum SSParameterListTag {
		ssSequenceTag = 0x30,
		ssSetTag = 0x31,
	};
	enum SSParameterTypeTag {
		ssOctetParam = 0x04,
	};


	SSComponentTypeTag ssComponentType;
	SSOperationCode ssOpCode;
	vector<string> ssParams;
	Bool_z ssParseSuccess;

	public:
	unsigned ssInvokeID;
	unsigned ssLinkID;

	string text()
	{
		string result = format("ComponentType=0x%x invokeID=0x%x linkID=0x%x opCode=0x%x ok=%d",
			ssComponentType,ssInvokeID,ssLinkID,ssOpCode,(int)ssParseSuccess);
		for (vector<string>::iterator it = ssParams.begin(); it != ssParams.end(); it++) {
			result += " <" + data2hex(it->data(),it->size()) + ">";
		}
		return result;
	}

	private:
	string getParam(unsigned paramNum,const char *opname)
	{
		if (paramNum >= ssParams.size()) {
			throw SSParseExcept(format("%s wrong number of params (%u)",opname,ssParams.size()));
		}
		return ssParams[paramNum];
	}

	unsigned ssParseLinkedID(const unsigned char *data, unsigned datalen, unsigned &datai)
	{
		// This is an optional componenent, whatever that means
		if (data[datai] != ssLinkedIDTag) { return 0; }
		datai++;
		unsigned linkIDLen = data[datai++];
		if (linkIDLen != 1) { throw SSParseExcept(format("unexpected link ID length: %d",linkIDLen)); }
		unsigned linkID = data[datai++];
		return linkID;
	}
	unsigned ssParseInvokeID(const unsigned char *data, unsigned datalen, unsigned &datai)
	{
		if (data[datai++] != ssInvokeIDTag) { throw SSParseExcept("missing required Invoke ID Tag"); }
		unsigned invokeIDLen = data[datai++];
		if (invokeIDLen != 1) { throw SSParseExcept(format("unexpected invoke ID length: %u",invokeIDLen)); }
		unsigned invokeID = data[datai++];
		return invokeID;
	}

	SSOperationCode ssParseOperationCode(const unsigned char *data, unsigned datalen, unsigned &datai)
	{
		if (data[datai++] != ssOperationCodeTag) { throw SSParseExcept("no operation code tag"); }
		unsigned operationCodeLen = data[datai++];
		if (operationCodeLen != 1) { throw SSParseExcept(format("unexpected operation code length: %u at %u",operationCodeLen,datai)); }
		SSOperationCode ssOpCode = (SSOperationCode) data[datai++];	// Finally, the piece of data we want.
		return ssOpCode;
	}

	string ssParseParameter(const unsigned char *data, unsigned datalen, unsigned &datai)
	{
		SSParameterTypeTag tag = (SSParameterTypeTag) data[datai++];
		if (tag != ssOctetParam) {
			throw SSParseExcept(format("Unexpected parameter type %u, expected %u at %u",tag,ssOctetParam,datai));
		}
		unsigned paramLen = data[datai++];
		if (paramLen > datalen-datai) { throw SSParseExcept(format("parameter len (%u) exceeds parameter list len (%u)",paramLen,datalen)); }
		// Finally, a parameter:
		string result = string((char*)&data[datai],paramLen);
		LOG(DEBUG) <<LOGVAR(paramLen)<<LOGVAR(datalen)<<LOGVAR(datai)<<LOGVAR(result.size());
		datai += paramLen;
		return result;
	}

	void ssParseParameterList(const unsigned char *data, unsigned datalen, unsigned &datai)
	{
		SSParameterListTag tag = (SSParameterListTag) data[datai++];
		if (tag != ssSequenceTag && tag != ssSetTag) { throw SSParseExcept(format("unexpected parameter tag: %u",tag)); }
		unsigned paramsLen = data[datai++];
		if (paramsLen < datalen-datai) { throw SSParseExcept(format("parameter len (%u) exceeds component len (%u) at %u",paramsLen,datalen-datai,datai)); }
		ssParams.clear();
		ssParams.reserve(6);
		unsigned adjustedLen = datai + paramsLen;
		LOG(DEBUG)<<LOGVAR(datai)<<LOGVAR(datalen)<<LOGVAR(paramsLen)<<LOGVAR(adjustedLen);
		while (datai < adjustedLen) {
			ssParams.push_back(ssParseParameter(data,adjustedLen,datai));
			LOG(DEBUG)<<LOGVAR(datai)<<LOGVAR(datalen)<<LOGVAR(paramsLen)<<LOGVAR(adjustedLen);
		};
#if 0
		const char*paramsStart = data+datai;
		unsigned parami = 0;
		while (parami < paramsLen) {
			LOG(DEBUG)<<LOGVAR(datai)<<LOGVAR(datalen)<<LOGVAR(paramsLen);
			ssParams.push_back(ssParseParameter(paramsStart,paramsLen,parami));
		};
		datai += parami;
#endif
	}

	// Parse the map message in the SS Facility IE.
	void ssParseDataInternal(const unsigned char *data, unsigned datalen, unsigned &datai)
	{
		if (datalen < 2) {
			throw SSParseExcept("data too short");
		}

		ssComponentType = (SSComponentTypeTag) data[datai++];
		unsigned componentLen = data[datai++];
		if (componentLen > datalen-datai) { throw SSParseExcept(format("component len (%u) exceeds data len-2 (%u) at %u",componentLen,datalen,datai)); }
		ssInvokeID = ssParseInvokeID(data,componentLen,datai);
		ssLinkID = ssParseLinkedID(data,datalen,datai);
		ssOpCode = ssParseOperationCode(data,componentLen,datai);
		ssParseParameterList(data,componentLen,datai);
	}

	public:

	// The data is the 'components" section of the facility IE.
	bool ssParseData(const unsigned char *data, unsigned datalen)
	{
		try {
			unsigned datai = 0;
			ssParseDataInternal(data, datalen, datai);
			ssParseSuccess = true;
			return true;
		} catch (SSParseExcept &err) {
			LOG(ERR) << "Error parsing Supplementary Service message:"<<err.mErrorMessage;
		} catch (...) {
			LOG(ERR) << "Unknown error parsing Supplementary Service message";	// should never happen.
		}
		return false;
	}

	SSMapCommand(const unsigned char *data, unsigned datalen) { ssParseData(data, datalen); }

	// Pull the USSD string out of the map command and return it.
	string ssGetUssd()
	{
		if (!ssParseSuccess) { return ""; }
		// TODO: something about this?
		switch (ssComponentType) {
			case ssInvokeTag:
			case ssReturnResultTag:
			case ssReturnErrorTag:
			case ssRejectTag:
			default:
				break;
		}

		string ussd;
		const char *opname;
		switch (ssOpCode) {
			case processUnstructuredSSData:		// USSD version 1 MS->network request.
				opname = "processUnstructuredSSData";
				ussd = ussdDecode(getParam(0,opname));
				break;
			case unstructuredSSNotify: 			// version 2 notification.
				opname = "unstructuredSSNotify";
				ussd = ussdDecode(getParam(1,opname));
				break;
			case processUnstructuredSSRequest:		// USSD version 1 MS->network request.
				opname = "processUnstructuredSSRequest";
				ussd = ussdDecode(getParam(1,opname));
				break;
			case activateSS:
				opname = "activateSS";
				ussd = format("*%s#",supServCodes.ssMapIdToServiceCodeDigits(getParam(0,opname)[0]));
				break;
			case registerSS:
				opname = "registerSS";
				ussd = "**";
				addParams:
				ussd += supServCodes.ssMapIdToServiceCodeDigits(getParam(0,opname)[0]);
				for (unsigned i = 1; i < ssParams.size(); i++) {
					ussd += "*";
					ussd += getParam(i,opname);
				}
				ussd += "#";
				break;
			case deactivateSS:
				opname = "deactivateSS";
				ussd = "#";
				goto addParams;
			case interrogateSS:
				opname = "interrogateSS";
				ussd = "*#";
				goto addParams;
			case eraseSS:
				opname = "eraseSS";
				ussd = "##";
				goto addParams;
			default:
				ussd = format("(unimplemented MAP SS-OP-CODE 0x%x)",ssOpCode);
		}
		return ussd;
	}
};


#if TODO_IN_PROGRESS
// There is a huge NonCall SS message parser in features/ussd, but USSD uses only a tiny subset
// of the SS Facility, so we will code it directly here.
// 3GPP 24.80 3.6
class L3USSDMessage {
	string ussdDataCodingScheme;
	string ussdString;
	bool ussdParse(int len, const unsigned char *components) {
	}
};
#endif

string ssMap2Ussd(const unsigned char *mapcmd,unsigned maplen)
{
	SSMapCommand cmd(mapcmd,maplen);
	LOG(DEBUG) << cmd.text();
	return cmd.ssGetUssd();
}

// SS can be in-call or stand-alone via CMServiceRequest.  This is the latter.
class MOSSDMachine : public SSDBase {
	enum State {	// These are the machineRunState states for our State Machine.
		stateStartUnused,	// unused.
		stateSSIdentResult
	};
	bool mIdentifyResult;
	unsigned mInvokeId;	// Copied from the USSD request to the USSD response.
	public:
	MachineStatus machineRunState(int state, const GSM::L3Message* l3msg, const SIP::DialogMessage *sipmsg);
	MOSSDMachine(TranEntry *wTran) : SSDBase(wTran) {}
	const char *debugName() const { return "MOSSDMachine"; }
	void sendUssdMsg(string ussdin, bool final);
};

// (pat) Used for SS messages arriving in Call Control messsages.
// Should be called mishandle, since I dont know what to do about this case.
// When I tried to send USSD messages within a call on the Blackberry, it disallowed it,
// so I dont really know what SS messages might show up within call control messages.
MachineStatus SSDBase::handleSSMessage(const GSM::L3Message*l3msg)
{
	// TODO: If there is an SS machine running, send the message there, otherwise error out.
	switch (l3msg->MTI()) {
		case L3SupServMessage::Register: {
			const L3SupServRegisterMessage *ssregmsg = dynamic_cast<typeof(ssregmsg)>(l3msg);
			LOG(ERR) << "Unexpected SS Register message:"<<ssregmsg;
			return MachineStatusOK;
			}
		case L3SupServMessage::Facility: {
			const L3SupServFacilityMessage *ssfacmsg = dynamic_cast<typeof(ssfacmsg)>(l3msg);
			LOG(ERR) << "Unexpected SS Facility message:"<<ssfacmsg;
			return MachineStatusOK;
			}
		case L3SupServMessage::ReleaseComplete: {
			const L3SupServReleaseCompleteMessage *ssrelmsg = dynamic_cast<typeof(ssrelmsg)>(l3msg);
			LOG(ERR) << "Unexpected SS Release Complete message:"<<ssrelmsg;
			return MachineStatusOK;
			}
	}
	return MachineStatusOK;		// not reached but makes g++ happy
}

void MOSSDMachine::sendUssdMsg(string ussdin, bool final)
{
	string ussd = ussdEncode(ussdin);

	// Make the stupid MAP message.
	char mapbuf[260];
	mapbuf[0] = 0xa1; 	// component type tag = Invoke (24.080 3.6.2)
	mapbuf[1] = ussd.size() + 13; 	// component length.
	mapbuf[2] = 0x02;	// component id tag == InvokeID
	mapbuf[3] = 0x01;	// invoke id length
	mapbuf[4] = this->mInvokeId;	// Must be copied from original USSD request.
	mapbuf[5] = 0x02;	// OperationCodeTag
	mapbuf[6] = 0x01;	// OperationCode length
	mapbuf[7] = unstructuredSSNotify;	// The MAP message type.
	mapbuf[8] = 0x30;	// Sequence tag.
	mapbuf[9] = ussd.size() + 5;	// Sequence length.
	// First argument is language type.
	mapbuf[10] = 0x04; 		// octet data?
	mapbuf[11] = 1; 	// length of param
	mapbuf[12] = 0x0f; 	// Default alphabet.	TODO - check this.
	// Second argument is the encoded ussd data.
	mapbuf[13] = 0x04; 	// octet data?
	mapbuf[14] = ussd.size();	// length of param
	memcpy(&mapbuf[15],ussd.data(),ussd.size());

	string components = string(mapbuf,15+ussd.size());
	LOG(DEBUG) << "map="<<data2hex(components.data(),components.size());
	L3SupServFacilityIE facility(components);
	if (final) {	// Is it the final USSD message?
		L3SupServReleaseCompleteMessage ssrelease(getL3TI(), facility);
		channel()->l3sendm(ssrelease);
	} else {
		L3SupServFacilityMessage ssfac(getL3TI(), facility);
		channel()->l3sendm(ssfac);
	}
}


void startMOSSD(const L3CMServiceRequest*cmsrq,MMContext *mmchan)
{
	LOG(DEBUG) <<mmchan;
	TranEntry *tran = TranEntry::newMOSSD(mmchan);
	string proxyUssd = gConfig.getStr("SIP.Proxy.USSD");
	if (proxyUssd.size() == 0) {
		// Disabled.  Reject USSD immediately.
		mmchan->l3sendm(L3CMServiceReject(L3RejectCause::Service_Option_Not_Supported));
		return;
	}

	MOSSDMachine *ssmp = new MOSSDMachine(tran);
	// The message is CMServiceRequest.
	tran->lockAndStart(ssmp,(GSM::L3Message*)cmsrq);
}

MachineStatus MOSSDMachine::machineRunState(int state, const GSM::L3Message *l3msg, const SIP::DialogMessage *sipmsg)
{
	PROCLOG2(DEBUG,state)<<LOGVAR(l3msg)<<LOGVAR(sipmsg)<<LOGVAR2("imsi",tran()->subscriber());
	switch (state) {
		case L3CASE_MM(CMServiceRequest): {
			timerStart(TCancel,30*1000,TimerAbortTran);	// Just in case.
			const L3CMServiceRequest *req = dynamic_cast<typeof(req)>(l3msg);
			const GSM::L3MobileIdentity &mobileID = req->mobileID();	// Reference ok - the SM is going to copy it.

			// FIXME: We should only identify this the FIRST time.
			return machPush(new L3IdentifyMachine(tran(),mobileID, &mIdentifyResult), stateSSIdentResult);
		}
		case stateSSIdentResult: {
			if (! mIdentifyResult) {
				//const L3CMServiceReject reject = L3CMServiceReject(L3RejectCause::Invalid_Mandatory_Information);
				channel()->l3sendm(L3CMServiceReject(L3RejectCause::Invalid_Mandatory_Information));
				return MachineStatus::QuitTran(TermCause::Local(L3RejectCause::Invalid_Mandatory_Information));
			}

			PROCLOG(DEBUG) << "sending CMServiceAccept";
			WATCH("SS CMServiceAccept");
			channel()->l3sendm(GSM::L3CMServiceAccept());		// On SAPI0

			gReports.incr("OpenBTS.GSM.SMS.MOSMS.Start");
			return MachineStatusOK;
		}

		case L3CASE_SS(L3SupServMessage::Register): {
			const L3SupServRegisterMessage *regp = dynamic_cast<typeof(regp)>(l3msg);
			// The TI comes from the other side, so we have to set the high bit when we respond.
			tran()->setL3TI(0x8 | regp->TI());
			WATCH("SS Register " << regp);
			string content = regp->getMapComponents();
			SSMapCommand mapcmd((const unsigned char *)content.data(),content.size());
			string ussd = mapcmd.ssGetUssd();
			this->mInvokeId = mapcmd.ssInvokeID;
			SIP::SipDialog::newSipDialogMOUssd(tran()->tranID(),tran()->subscriber(),ussd,channel());
			return MachineStatusOK;
		}
		case L3CASE_SS(L3SupServMessage::Facility): {
			const L3SupServFacilityMessage *facp = dynamic_cast<typeof(facp)>(l3msg);
			WATCH("SS Facility " << facp);
			return MachineStatusOK;
		}
		case L3CASE_SS(L3SupServMessage::ReleaseComplete): {
			const L3SupServFacilityMessage *relp = dynamic_cast<typeof(relp)>(l3msg);
			WATCH("SS ReleaseComplete" << relp);
			return MachineStatus::QuitTran(TermCause::Local(L3Cause::USSD_Success));
		}

		case L3CASE_SIP(dialogBye): {
			if (sipmsg == NULL) {
				LOG(ERR) << "USSD client error: missing BYE message";
				return MachineStatus::QuitTran(TermCause::Local(L3Cause::USSD_Error));
			}
			const DialogUssdMessage *umsg = dynamic_cast<typeof(umsg)>(sipmsg);
			if (umsg == NULL) {
				LOG(ERR) << "USSD client error: could not convert DialogMessage to DialogUssdMessage "<<sipmsg;
				return MachineStatus::QuitTran(TermCause::Local(L3Cause::USSD_Error));
			}
			string result = umsg->dmMsgPayload;
			// Send it to the MS.
			// Sending the message in the Facility IE of ReleaseComplete did not work,
			// but sending a separate Facility message does, so fine, do it that way.
			sendUssdMsg(result, false);	// Send an L3Facility message.
			L3SupServReleaseCompleteMessage ssrelease(getL3TI());
			channel()->l3sendm(ssrelease);
			//sendUssdMsg(result, true);
			return MachineStatus::QuitTran(TermCause::Local(L3Cause::USSD_Success));
		}

		default:
			LOG(DEBUG) << "unexpected state";
			return unexpectedState(state,l3msg);
	}
}

};	// namespace
