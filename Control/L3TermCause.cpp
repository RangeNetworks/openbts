/**@file Declarations for common-use control-layer functions. */
/*
* Copyright 2013 Range Networks, Inc.
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
#define LOG_GROUP LogGroup::Control
#include <stdio.h>	// For snprintf

#include <Defines.h>
#include <L3TermCause.h>
#include <Logger.h>
#include <OpenBTSConfig.h>
#include <SIPDialog.h>

namespace Control {
using std::string;
using namespace GSM;

// The default SIP <-> L3-CC-Cause mapping an be overridden with config options.
// For this purpose the GSM Layer3 causes are identified by using the names of the causes
// as defined in GSM 4.08 with space replaced by underbar and any other special chars removed.  See file L3Enums.h
// Config option Control.Termination.CauseToSIP.<causename> specifies the SIP code, and optionally, the SIP reason phrase,
// to be sent to the SIP peer when this L3 Cause occurs.
// Config option Control.Termination.SIPToCause.<SIP-code> specifies the name of the layer3 cause to be used for a specific SIP code.
// The Layer3 cause is passed to the handset to indicate the message to display, and also may be saved in the CDR.
// To make this easier for the user we will use the L3 cause as the SIP reason phrase, so the reason phrase can
// be placed in the config option to control it.

static int cause2SipCodeFromConfig(AnyCause acause, string &reason)
{
	// User is allowed to over-ride default sipcode for each cause in configuration options:
	const char *causeName = L3Cause::AnyCause2Str(acause);
	char configOptionName[100];
	snprintf(configOptionName,100,"Control.Termination.CauseToSIP.%s",causeName);
	if (gConfig.defines(configOptionName)) {
		string value = gConfig.getStr(configOptionName);
		// Value must begin with a positive number; ignore leading space.
		const char *vp = value.c_str();
		while (isspace(*vp)) { vp++; }
		if (!isdigit(*vp)) {
			LOG(ERR) << "Invalid config value for '"<<configOptionName<<"' ignored:"<<value;
			return 0;
		}
		int sipcode = atoi(vp);
		// See if the sip code is followed by a non-empty string - if so use it as the SIP reason phrase.
		if ((vp = strchr(vp,' '))) {
			while (isspace(*vp)) vp++;
			if (*vp == 0) return 0;
			reason = string(vp);
		}
		return sipcode;
	}
	return 0;
}

static int cause2SipCode(AnyCause acause, string& reason)
{
	int sip = cause2SipCodeFromConfig(acause, reason);
	if (reason.empty()) { reason = string(L3Cause::AnyCause2Str(acause)); }
	if (sip) { return sip; }

	sip = 408;	// Default SIP for almost all causes is Timeout
	switch (acause.value) {
		// These are CC Causes:
		case L3Cause::Unknown_L3_Cause: sip = 486; break;  				// Busy
		case L3Cause::Unassigned_Number: sip = 404; break; 				// Not Found
		case L3Cause::No_Route_To_Destination: sip = 404; break; 		// Not found
		case L3Cause::Channel_Unacceptable: sip = 503; break;			// Service Unavailable.
		case L3Cause::Operator_Determined_Barring: sip = 503; break; 	// Service Unavailable.
		case L3Cause::Normal_Call_Clearing: sip = 200; break;			// OK
		case L3Cause::User_Busy: sip = 486; break;						// Busy Here
		case L3Cause::No_User_Responding: sip = 408; break;				// Request Timeout
		case L3Cause::User_Alerting_No_Answer: sip = 480; break; 		// Temporarily Unavailable.
		// RFC3261 says use 403 instead of 603 unless we know with certainty there is no second choice like forwarding or voicemail.
		case L3Cause::Call_Rejected: sip = 403; break; 					// Forbidden.
		case L3Cause::Number_Changed: sip = 410; break; 				// Gone
		// Preemption,
		// Non_Selected_User_Clearing,
		// Destination_Out_Of_Order,
		case L3Cause::Invalid_Number_Format: sip = 484; break;	// Address Incomplete
		case L3Cause::Facility_Rejected: sip = 503; break;		// Service Unavailable.
		// Response_To_STATUS_ENQUIRY,
		case L3Cause::Normal_Unspecified: sip = 200; break;	// OK
		case L3Cause::No_Channel_Available: sip = 503; break; // Service Unavailable.
		//  Network_Out_Of_Order, 
		//  Temporary_Failure, 
		//  Switching_Equipment_Congestion, 
		//  Access_Information_Discarded, 
		case L3Cause::Requested_Channel_Not_Available: 
		case L3Cause::Resources_Unavailable: sip = 503; break;
		//  Quality_Of_Service_Unavailable, 
		//  Requested_Facility_Not_Subscribed, 
		//  Incoming_Calls_Barred_Within_CUG, 
		//  Bearer_Capability_Not_Authorized, 
		//  Bearer_Capability_Not_Presently_Available, 
		//  Service_Or_Option_Not_Available, 
		//  Bearer_Service_Not_Implemented, 
		//  ACM_GE_Max, 
		//  Requested_Facility_Not_Implemented, 
		//  Only_Restricted_Digital_Information_Bearer_Capability_Is_Available, 
		//  Service_Or_Option_Not_Implemented, 
		//  Invalid_Transaction_Identifier_Value, 
		//  User_Not_Member_Of_CUG, 
		//  Incompatible_Destination, 
		//  Invalid_Transit_Network_Selection, 
		//  Semantically_Incorrect_Message, 
		//  Invalid_Mandatory_Information, 
		//  Message_Type_Not_Implemented, 
		//  Messagetype_Not_Compatible_With_Protocol_State, 
		//  IE_Not_Implemented, 
		//  Conditional_IE_Error, 
		//  Message_Not_Compatible_With_Protocol_State, 
		//  Recovery_On_Timer_Expiry, 
		//  Protocol_Error_Unspecified, 
		//  Interworking_Unspecified, 

		// We dont bother putting the BSSCause or MMCause in this switch; they all just map to the default SIP code.

		// These are the Custom Causes:
		// Make sure No_Paging_Reponse and IMSI_Detached map to 480 Temporarily Unavailable even if someone
		// changes the default up above.
		case L3Cause::No_Paging_Response: sip = 480; break;
		case L3Cause::IMSI_Detached: sip = 480; break;

		default: break;
	}
	return sip;
}

int TermCause::tcGetSipCodeAndReason(string &sipreason)
{
	if (mtcSipCode) {
		sipreason = mtcSipReason;
		return mtcSipCode;
	}
	return cause2SipCode(mtcAnyCause, sipreason);
}

string TermCause::tcGetSipReasonHeader()
{
	// Q.850 causes do not exactly match 3GPP causes, so we have to fix it:
	int q850cause;
	switch (L3Cause::CCCause cccause = tcGetCCCause()) {
		case L3Cause::Preemption:
			q850cause = 8;	// This is the Q.850 cause value that corresponds to the Layer 3 CC-cause "Premption".
			break;
		default:
			q850cause = cccause;	// All other CC-Causes that we use have the same values as corresponding Q.850 causes.
			break;
	}

	// In SIP, the dialog termination may be indicated by one of: an error reply, BYE or CANCEL message.
	// We ship the full text of the actual cause to the peer.  For an error reply it is in the SIP reason phrase;
	// for the BYE and CANCEL it is in the only in the Reason: header.
	string text = string(L3Cause::AnyCause2Str(mtcAnyCause));
	return format("Q.850;cause=%d ;text=\"%s\"",q850cause,text);
}

// MSC-BSS Causes are in GSM 08.08 3.2.2.5.  They include:
//		RR:RadioInterfaceFailure=1, RR:UplinkQuality=2, RR:UplinkStrength=3, RR:DownlinkQuality=4, RR:DownlinkStrength=5, RR:Distance=6,
//		BTS:O&MIntervention=7,
// 		RR:ChannelAssignmentFailure=0xa (called: Radio Interface Failure, reversion to old channel)
//		BTS:HandoverSuccessful=0xb, BetterCell=0xc, RR:NoRadioResourceAvailable=0x21, RR:CCCHOverload=0x23,
//		BTS:Preemption=0x29 (emergency), TranscodingMismatch=0x30,
//		RequestedSpeechVersinUnavailable=0x33
//		BTS:CipheringAlgorithmNotSupported=0x40
//	These would all map to L3Cause::NormalUnspecified to the other handset.
// MAP-Errors are in GSM 09.02 17.6.6
// 		MAP Absent-Subscriber error is used for NoPagingResponse, IMSI Detached, Restricted Area for CS services;
//			also GPRS Detached, others for other services.
// MM Mobility Management errors in 4.08 10.5.3.6 Reject Cause
// Termination Possibilities.
// The events we recognize are:
//		A or B-Hangup L3Cause::NormalCallClearing A or B  SIP 0
//		B-Rejected L3Cause::CallRejected B SIP 403
//		A-MOCancel (during ringing) CDR:a-abort
//		A-MOCancelPowerOff (during ringing): CDR:a-abort
//		B-RingNoAnswer L3Cause::UserAlertingNoAnswer 480
//		B-Off (before ringing)  A gets: L3Cause::DestinationOutOfOrder SIP 404
//				Could use Q850 20 UserAbsent (20) which is not used by L3Cause
//		B-RingPowerOff (during ringing): L3Cause::CallRejected B
//		B-NoAnswerToPage L3Cause::DestinationOutOfOrder   SIP 408
//		B-Busy  SIP 486
//		A or B-PreemptionByOperator
//		A or B-PreemptionByEmergency
//		A or B-ConnectionLost (many reasons - MSC-BSS causes)
//		Congestion
//	MM Layer failures.  If these happen at the MTC end, we should send code upward.  In all cases A gets L3Cause::NoUserResponding?
//		AuthenticationReject (at the 4.08 level this is a message no specific code, but we can use the Control.LUR.UnprovisionedRejectCause
//		Many other failures possible at higher level: IMSI unknown, IMSI invalid, no number configured, use MM or MAP reject cause.
//		


// void initSipCode()
// {
// 	struct SipCodeKey : public ConfigurationKey {
// 		char namebuf[100];
// 		char defaultValueBuf[40];
// 
// 		SipCodeKey(ConfigurationKeyMap &map,const char *name, int defaultValue, const char *units, const char *range, const char *help = "")
// 		{
// 			snprintf(namebuf,sizeof(namebuf),"Layer3.TerminationCause.%s",name);
// 			snprintf(defaultValueBuf,sizeof(defaultValueBuf),"%d",defaultValue);
// 			ConfigurationKey tmp(namebuf,
// 				defaultValueBuf,
// 				units,
// 				ConfigurationKey::DEVELOPER,
// 				ConfigurationKey::VALRANGE,
// 				range,
// 				false,
// 				help);
// 			map[namebuf] = tmp;
// 		}
// 	};
// }

//	Event:CallRejected.SIP	"403 Call Rejected"
//	Event:Cancel	# There is no SIP for this because it is a 
//		The MT end needs a code.  The MOC doesnt
//	Event:IMSIDetach
//	Event:NoPagingResponse
//	Event:UserBusy
//	Event::AuthorizationFailure Reason:MMRejectCause from 24.008 10.5.3.6
//	Event::NoChannelAvailable Reason:BSS Cause GSM*.08-3.2.2.5
//	Event::RadioLoss  Reason:BSS Cause GSM*.08-3.2.2.5
//	Event::Disconnect (Disconnect, Release or ChannelRelease message)  Reason:L3Cause passed by handset in message.
//	Event::OperatorIntervention
//	Event::Preemption

//	An "Event" is something that causes connection termination.
//		The Event Location can be local handset, local BTS (radio problems), network, remote BTS or remote handset.
//		The Locus can be CC (messages from handset), MM, RR, CN (core network)
//		Note: The Layer3 CC Cause includes messages from all loci.

// Termination before connection:
//	Event:AlertingNoAnswer
// 		MTC rings, no answer; MOC gets L3Cause::UserAlertingNoAnswer; SIP 408 (Request Timeout) (See L3StateMachine.cpp)  Lynx3.1.3 CDR:No-answer
//	Event:CallRejected
// 		MTC declines (aborts while ringing) MOC gets L3Cause::CallRejected; SIP 403: Forbidden (per RFC3261 sec 15) Lynx3.1.4 CDR:B-party-abort
//	Event:Cancel
//		MOC cancel (hangs up while ringing) MTC gets L3Cause::NormalCallClearing?;  MOC sends SIP CANCEL, MTC sends 487 Request Terminated
//			Lynx3.1.5: CDR:A-party-abort.     could argue "NormalCallClearing" is reserved for termination after connect.
//			Pat says: how do we represent this in the CDR?  This is A-party normal hangup with 0 seconds connect time.
//	Event:IMSIDetach
//		MOC IMSIDetach during ringing; identical to MOC Cancel Lynx3.1.9:CDR:A-party-abort
//		MTC IMSIDetach before ringing; Lynx3.1.10:CDR:None, which is distinct from no answer to page.
//		MTC IMSIDetach during ringing Lynx3.1.13:CDR:b-party-abort
//	Event:NoPagingResponse
//		MTC NoPagingResponse (off or outside coverage area); L3Cause::DestinationOutOfOrder;  Lynx3.1.6&7:CDR:No-page-response
//			either SIP 408 (Request Timeout) or 504 (Server Timeout)  404 (Not Found) doesnt seem right because the server
//				has definitive info that user does not exist, but in our case a new page may succeed.
//	Event:UserBusy
// 		MTC busy (has existing call, and call-hold/call-wait not available)  MOC gets SIP 486 (not 600) Lynx3.1.8:CDR:B-party-busy
//	Event::AuthorizationFailure Reason:MMRejectCause from 24.008 10.5.3.6
//		MOC SIM Invalid. Lynx3.1.14:no call.
//		MTC SIM invalid. Lynx3.1.15: a-party told "invalid subscriber"?
//		MTC called number not in SR (vacant) Lynx3.1.15:CDR:invalid-number
//	Event::NoChannelAvailable Reason:BSS Cause GSM*.08-3.2.2.5
//		MTC congestion; L3Cause::SwitchingEquipmentCongestion SIP 503 Service Unavailable Lynx3.1.17:CDR:None.
//	Event::RadioLoss  Reason:BSS Cause GSM*.08-3.2.2.5
//		MTC radio loss; MOC gets L3Cause NoUserResponding,  SIP 504 No User Responding;  Lynx:not-specified
//		MOC radio loss; MTC gets Cancel.  Lynx:not-specified
// Termination after connection:
// Note: The BYE message needs a "Reason" to distinguish these apart for CDR purposes.
//	Event::Disconnect (Disconnect, Release or ChannelRelease message)  Reason:L3Cause passed by handset in message.
// 		MOC (A-party) disconnect; L3Cause::NormalCallClearing; SIP BYE, no sip code, Reason:none Lynx3.1.1 CDR:A-party-normal-disconnect
// 		MTC (B-party) disconnect; L3Cause::NormalCallClearing; SIP BYE, no sip code Reason:none Lynx3.1.2 CDR:B-party-normal-disconnect
//	Event::OperatorIntervention
// 		Operator Intervention.	L3Cause::Preemption SIP 480 (Temporarily Unavailable) or 503 (Service Unavailable)
//	Event::Preemption
// 		Preemption by Emergency Call. L3Cause::Preemption  SIP 480 (Temporarily Unavailable) or 503 (Service Unavailable)
//	Event::RadioLoss  Reason:BSS Cause GSM*.08-3.2.2.5
//		MOC or MTC connection lost (simulated by MS turned off, which is not really correct since it generates a detach); RRcause?  SIP BYE?
//			Lynx 3.1.11&3.1.12:CDR:Termination-error if MOC or CDR:B-party-abort if MTC lost.  That's just dopey.
//			We can use L3cause::NormalUnspecified, which we do not use for anything else.
//		Peer gets SIP BYE,


// Return a call-control cause, formerly aka L3Cause, from GSM 4.08 10.5.4.11.
// Call-control causes are meant for communication to the handset, and in the case where we are sending a CC cause to the handset,
// the cause will already always be a CC cause.
// But we are also sending them to the outside world in SIP, and maybe we wills tore them in the CDRs as well,
// so if it is not a call-control cause, return the nearest match.
L3Cause::CCCause TermCause::tcGetCCCause()
{
	switch (mtcAnyCause.getLocus()) {
		case L3Cause::LocusCC:
			return mtcAnyCause.ccCause;
		case L3Cause::LocusMM:
			// Authentication falure.
			return L3Cause::Operator_Determined_Barring;
		case L3Cause::LocusBSS:
			// Radio or other failure 
			switch (mtcAnyCause.bssCause) {
				case L3Cause::Radio_Interface_Failure:
				case L3Cause::Uplink_Quality:
				case L3Cause::Uplink_Strength:
				case L3Cause::Downlink_Quality:
				case L3Cause::Downlink_Strength:
				case L3Cause::Distance:
					return L3Cause::Destination_Out_Of_Order;
				case L3Cause::Operator_Intervention:
					return L3Cause::Preemption;
				case L3Cause::Channel_Assignment_Failure:
					return L3Cause::No_Channel_Available;
				case L3Cause::Handover_Successful:
				case L3Cause::Better_Cell:
				case L3Cause::Traffic:
				case L3Cause::Reduce_Load_In_Serving_Cell:
				case L3Cause::Traffic_Load_In_Target_Cell_Higher_Than_In_Source_Cell:
				case L3Cause::Relocation_Triggered:
					break;	// These should not occur.
				case L3Cause::Equipment_Failure:
				case L3Cause::No_Radio_Resource_Available:
				case L3Cause::CCCH_Overload:
				case L3Cause::Processor_Overload:
				case L3Cause::Traffic_Load:
					return L3Cause::No_Channel_Available;
				case L3Cause::Emergency_Preemption:
					return L3Cause::Preemption;
				case L3Cause::DTM_Handover_SGSN_Failure:
				case L3Cause::DTM_Handover_PS_Allocation_Failure:
				case L3Cause::Transcoding_Mismatch:
				case L3Cause::Requested_Speech_Version_Unavailable:
				case L3Cause::Ciphering_Algorithm_Not_Supported:
					break;	// Not sure what we would send, but these do not occur so dont worry about it.
			}
			// Some kind of error occurred.
			return L3Cause::No_User_Responding;	// Our all purpose default cause.
		case L3Cause::LocusCustom:
			switch (mtcAnyCause.customCause) {
				case L3Cause::No_Paging_Response:
				case L3Cause::IMSI_Detached:		// This is a MAP error code, which would have to map to l3cause DestinationOutOfOrder.
					return L3Cause::Destination_Out_Of_Order;
				case L3Cause::Handover_Outbound:
				case L3Cause::Handover_Error:			// Logic error during handover.
				case L3Cause::Invalid_Handover_Message:
					break;	// errors
				case L3Cause::Missing_Called_Party_Number:
					return L3Cause::Invalid_Mandatory_Information;
				case L3Cause::Layer2_Error:
				case L3Cause::Sip_Internal_Error:
				case L3Cause::L3_Internal_Error:
				case L3Cause::No_Transaction_Expected:	// Used when we dont expect termination to have any transactions to close out.
				case L3Cause::Already_Closed:			// Used when we know for sure the transaction has already been terminated.
				case L3Cause::SMS_Timeout:
				case L3Cause::SMS_Error:
					break;	// errors
				case L3Cause::SMS_Success:
					return L3Cause::Normal_Call_Clearing;
				case L3Cause::USSD_Error:
					break;	// errors
				case L3Cause::USSD_Success:
				case L3Cause::MM_Success:
					return L3Cause::Normal_Call_Clearing;
			}
			// Some kind of error occurred.
			return L3Cause::No_User_Responding;	// Our all purpose default cause.
		default:
			devassert(0);
			return L3Cause::Normal_Unspecified;
	}
}

TermCause dialog2ByeCause(SIP::SipDialog *dialog)
{
	// TODO:
	return TermCause::Remote(L3Cause::Normal_Call_Clearing,0,"Bye");
}

static int sipCode2AnyCauseFromConfig(int sipcode)
{
	// User is allowed to over-ride default sipcode for each cause in configuration options:
	char configOptionName[100];
	snprintf(configOptionName,100,"Control.Termination.SIPToCause.%d",sipcode);
	if (gConfig.defines(configOptionName)) {
		string causeName = gConfig.getStr(configOptionName);
		int cause = CauseName2Cause(causeName);
		if (cause) {
			return cause;
		} else {
			LOG(ERR) << "Config option '"<<configOptionName<<"' specifies unrecognized cause name:'"<<causeName<<"'";
		}
	}
	return 0;
}

int sipCode2AnyCause(int sipcode,	// The sip code from the dialog error.
	bool alerted)		// True if we ever received sip code 180.
{
	int acause = sipCode2AnyCauseFromConfig(sipcode);
	// Look up a default L3 cause from the SIP code.
	if (! acause) {
		switch (sipcode) {
			case 486: // Busy here
			case 600: // Busy everywhere
				acause = L3Cause::User_Busy;
				break;
			case 403: // Forbidden.  (RFC3261 sec 15 says use this for user-declined.)
			case 603: // Decline
				acause = L3Cause::Call_Rejected;
				break;
			case 480: // Temporarily unavailable
				acause = alerted ? L3Cause::User_Alerting_No_Answer : L3Cause::No_User_Responding;
				break;
			case 408: // Request Timeout, from many causes.
				acause = L3Cause::No_User_Responding;
				break;
			case 404:
				acause = L3Cause::No_Route_To_Destination;
				break;
			case 503: // SIP Service Unavailable.  Asterisk sends this often.
				acause = L3Cause::Resources_Unavailable;
				break;
			case 0:
				// This is most likely an early MTD Mobile Terminated Disconnect caused by CANCEL before ACK received.
				acause = L3Cause::User_Busy;
				break;
			default:
				// Operators dont want to show network failure on the phones, so use something else.
				//return TermCause(L3Cause::InterworkingUnspecified,sipcode,sipreason);
				acause = L3Cause::User_Busy;
				break;
		}
	}
	return acause;
}

// Return the Q.850 cause from a "Reason:" header, or 0 if none.
int parseReasonHeaderQ850(const char * reasonHeader)
{
	// Split at commas.
	const char *q850p = strcasestr(reasonHeader,"Q.850");
	if (!q850p) return 0;
	const char *commap = strchr(q850p,',');
	const char *causep = strcasestr(q850p,"cause");
	if (!causep || (commap && causep > commap)) return 0;
	causep += strlen("cause");
	int result;
	if (1 != sscanf(causep," = %d",&result)) return 0;
	return result;
}

// Search for a cause in the SIP message.
TermCause dialog2TermCause(SIP::SipDialog *dialog)
{
	if (! dialog) {		// Be ultra-cautious.
		// This is not supposed to happen.
		return TermCause::Remote(AnyCause(L3Cause::No_User_Responding),480,"No_User_Responding");
	}
	string sipreason;
	int sipcode = dialog->getLastResponseCode(sipreason);
	// Does the reason phrase look like one that was created on a peer OpenBTS?
	int l3cause = CauseName2Cause(sipreason);
	if (! l3cause) {
		string reasonHeader = dialog->getLastResponseReasonHeader();
		// Was a Q.850 reason included?
		int q850 = parseReasonHeaderQ850(reasonHeader.c_str());
		if (q850) {
			l3cause = (q850 == 8) ? L3Cause::Preemption : (CCCause) q850;
		} else {
			bool alerted = dialog->mReceived180;
			l3cause = sipCode2AnyCause(sipcode,alerted);
		}
	}
	return TermCause::Remote(AnyCause(l3cause),sipcode,sipreason);
}

std::ostream& operator<<(std::ostream& os, TermCause &cause)
{
	os <<format("l3cause=%d=0x%x(%s) SipCode=%d(%s)",cause.tcGetValue(),cause.tcGetValue(),L3Cause::AnyCause2Str(cause.mtcAnyCause),(int)cause.mtcSipCode,cause.mtcSipReason.c_str());
	return os;
}

};	// namespace Control
