/*
* Copyright 2008, 2014 Free Software Foundation, Inc.
*
* This software is distributed under multiple licenses; see the COPYING file in the main directory for licensing information for this specific distribution.
*
* This use of this software may be subject to additional restrictions.
* See the LEGAL file in the main directory for details.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

*/
#ifndef _L3ENUMS_H_
#define _L3ENUMS_H_ 1
#include <string>
#include <map>

namespace GSM {

// Maps string name of cause to an anycause.
extern std::map<std::string,int> gCauseNameMap;

// The values from the RR Cause Information Element.
/** GSM 04.08 10.5.2.31 */
struct L3RRCause {
	enum RRCause {
		Normal_Event = 0,
		Unspecified = 1,
		Channel_Unacceptable = 2,
		Timer_Expired = 3,
		No_Activity_On_The_Radio = 4,
		Preemptive_Release = 5,
		UTRAN_Configuration_Unknown = 6,
		Handover_Impossible = 8,	// (Timing out of range)
		Channel_Mode_Unacceptable = 9,
		Frequency_Not_Implemented = 0xa,
		Leaving_Group_Call_Area = 0xb,
		Lower_Layer_Failure = 0xc,
		Call_Already_Cleared = 0x41,
		Semantically_Incorrect_Message = 0x5f,
		Invalid_Mandatory_Information = 0x60,
		Message_Type_Invalid = 0x61,		// Not implemented or non-existent
		Message_Type_Not_Compapatible_With_Protocol_State = 0x62,
		Conditional_IE_Error = 0x64,
		No_Cell_Available = 0x65,
		Protocol_Error_Unspecified = 0x6f
	};
	static const char *RRCause2Str(RRCause cause);
};
typedef L3RRCause::RRCause RRCause;

/** RejectCause, GSM 04.08 10.5.3.6 */
// Better: 24.008 10.5.3.6
// This is the Mobility Management reject cause.
// For RR causes see L3RRCause, and for CC Causes see L3Cause.
struct L3RejectCause {
	public:
	enum RejectCause {
		Zero = 0,			// This is NOT a GSM RejectCause, it is our unspecified value.
		IMSI_Unknown_In_HLR = 2,
		Illegal_MS = 3,
		IMSI_Unknown_In_VLR =  4,
		IMEI_Not_Accepted =  5,
		Illegal_ME = 6,
		PLMN_Not_Allowed = 0xb,
		Location_Area_Not_Allowed = 0xc,
		Roaming_Not_Allowed_In_LA =  0xd,		// Roaming not allowed in this Location Area
		No_Suitable_Cells_In_LA = 0xf,
		Network_Failure =  0x11,
		MAC_Failure =  0x14,
		Synch_Failure =  0x15,
		Congestion =  0x16,
		GSM_Authentication_Unacceptable = 0x17,
		Not_Authorized_In_CSG = 0x19,
		Service_Option_Not_Supported = 0x20,
		Requested_Service_Option_Not_Subscribed = 0x21,
		Service_Option_Temporarily_Out_Of_Order = 0x22,
		Call_Cannot_Be_Identified = 0x26,
		// 0x30 - 0x3f : retry upon entry into a new cell ???
		Semantically_Incorrect_Message = 0x5f,
		Invalid_Mandatory_Information = 0x60,
		Message_Type_Invalid = 0x61,			// Message type non-existent or not implemented
		Message_Type_Not_Compatible_With_Protocol_State = 0x62,
		IE_Invalid = 0x63,						// IE non-existent or not implemented
		Conditional_IE_Error = 0x64,
		Message_Not_Compatible_With_Protocol_State = 0x65,
		Protocol_Error_Unspecified = 0x6f
	};
	static const char *rejectCause2Str(RejectCause cause);
};

typedef L3RejectCause::RejectCause MMRejectCause;

// All the other causes here; the L3Cause class is used primarily to provide a namespace wrapper.
// This (together with L3RejectCause, which is referenced by Locus below) encompasses the complete list of reasons a transaction can be terminated.
// See the TermCause class for conversion of these causes into other cause types (CC-cause, SIP error code, or Q.850 code.)
struct L3Cause {
	// GSM 4.08 10.5.4.11 Location as used in the Layer3 Cause Information Element.
	enum Location {
		User=0,
		Private_Serving_Local=1,
		Public_Serving_Local=2,
		Transit=3,
		Public_Serving_Remote=4,
		Private_Serving_Remote=5,
		International=7,
		Beyond_Inter_Networking=10
	};


	// GSM 4.08 10.5.4.11 call-control cause.  
	// Note that these are almost the same as ITU Recommendation Q.850 codes, but not quite - the Preemption value is different.
	enum CCCause {
		Unknown_L3_Cause = 0,			// Range addition.
		Unassigned_Number = 1,		// or unallocated number
		No_Route_To_Destination = 3,
		Channel_Unacceptable = 6,
		Operator_Determined_Barring = 8,
		Normal_Call_Clearing = 16,		// One of the two handsets hang up.
		User_Busy = 17,					// "the called user has indicated the inablity to accept another call"
		No_User_Responding = 18,			// (pat) I dont understand the H.1.7 description:
										// "This cause is used when a user does not respond to a call establishment message with either
										// "an alerting or connect indication within the prescribed period of time allocated
										// "(defined by the expiry of either timer T303 or T310)."
										// In other words, the MTC handset is present, but when the BTS sends setup it
										// does not respond with alerting.  Why not? Busy with GPRS?  Authentication failure?
		User_Alerting_No_Answer = 19,		// The phone rang, but the user did not pick up.
		Call_Rejected = 21,				// The user "does not wish to accept this call" although they are neither busy nor incompatible.
		Number_Changed = 22,
		Preemption = 25,				// Including pre-emption by emergency call.
		Non_Selected_User_Clearing = 26,
		Destination_Out_Of_Order = 27,		// user equipment off-line.  No answer to page.
		Invalid_Number_Format = 28,		// invalid or incomplete number
		Facility_Rejected = 29,
		Response_To_STATUS_ENQUIRY = 30,
		Normal_Unspecified = 31,
		No_Channel_Available = 34,
		Network_Out_Of_Order = 38,
		Temporary_Failure = 41,
		Switching_Equipment_Congestion = 42,
		Access_Information_Discarded = 43,
		Requested_Channel_Not_Available = 44,
		Resources_Unavailable = 47,
		Quality_Of_Service_Unavailable = 49,
		Requested_Facility_Not_Subscribed = 50,
		Incoming_Calls_Barred_Within_CUG = 55,
		Bearer_Capability_Not_Authorized = 57,
		Bearer_Capability_Not_Presently_Available = 58,
		Service_Or_Option_Not_Available = 63,
		Bearer_Service_Not_Implemented = 65,
		ACM_GE_Max = 68,			// ACM greater or equal to ACM max.  Whatever that is.
		Requested_Facility_Not_Implemented = 69,
		Only_Restricted_Digital_Information_Bearer_Capability_Is_Available = 70,	// If you ever use, go ahead and abbreviate it.
		Service_Or_Option_Not_Implemented = 79,
		Invalid_Transaction_Identifier_Value = 81,
		User_Not_Member_Of_CUG = 87,
		Incompatible_Destination = 88,
		Invalid_Transit_Network_Selection = 91,
		Semantically_Incorrect_Message = 95,
		Invalid_Mandatory_Information = 96,
		Message_Type_Not_Implemented = 97,
		Messagetype_Not_Compatible_With_Protocol_State = 98,
		IE_Not_Implemented = 99,			// Information Element non-existent or not implemented.
		Conditional_IE_Error = 100,
		Message_Not_Compatible_With_Protocol_State = 101,
		Recovery_On_Timer_Expiry = 102,
		Protocol_Error_Unspecified = 111,
		Interworking_Unspecified = 127,
	};

	// We use an integer value to identify any one of these cause types.
	// The low byte of the cause value is the cause and the high byte is the Locus from this list.
	// We dont put RR causes here because they are somewhat orthogonal to the other causes here and add no new information.
	enum Locus {
		LocusCC = 0,			// Low byte is an L3Cause::CCCause
		LocusMM = 0x100,		// Low byte is an L3RejectCause::RejectCause
		LocusBSS = 0x200,		// Low byte is an L3Cause::BSSCause
		LocusCustom = 0x300,	// Low byte is an L3Cause::CustomCause.
	};

	// MSC-BSS Causes from GSM 48.008 3.2.2.5.  8.08 3.2.2.5 only defines a subset of these.
	// This is not the entire list, just the ones we might possibly use.
	enum BSSCause {
		Radio_Interface_Failure=1 | LocusBSS,
		Uplink_Quality=2 | LocusBSS,
		Uplink_Strength=3 | LocusBSS,
		Downlink_Quality=4 | LocusBSS,
		Downlink_Strength=5 | LocusBSS,
		Distance=6 | LocusBSS,
		Operator_Intervention=7 | LocusBSS,		// O&M Intervention BTS Operator Intervention.
		Channel_Assignment_Failure=0xa | LocusBSS, 	// (called: Radio Interface Failure, reversion to old channel)
		Handover_Successful=0xb | LocusBSS,	// BTS
		Better_Cell=0xc | LocusBSS,
		Traffic = 0xf | LocusBSS,

		// Only in 48.008:
		Reduce_Load_In_Serving_Cell = 0x10 | LocusBSS,
		Traffic_Load_In_Target_Cell_Higher_Than_In_Source_Cell = 0x11 | LocusBSS,
		Relocation_Triggered = 0x12 | LocusBSS,

		Equipment_Failure=0x20 | LocusBSS,
		No_Radio_Resource_Available=0x21 | LocusBSS,
		CCCH_Overload=0x23 | LocusBSS,
		Processor_Overload = 0x24 | LocusBSS,
		Traffic_Load=0x28 | LocusBSS,
		Emergency_Preemption=0x29 | LocusBSS,		// Emergency Preemption as opposed to Operator_Intervention  (Defined as just Preemption in 48.008, but we must distinguish from CCCause::Preemption.)
		DTM_Handover_SGSN_Failure = 0x2a | LocusBSS,
		DTM_Handover_PS_Allocation_Failure = 0x2b | LocusBSS,

		Transcoding_Mismatch=0x30 | LocusBSS,
		Requested_Speech_Version_Unavailable=0x33 | LocusBSS,

		Ciphering_Algorithm_Not_Supported=0x40 | LocusBSS,
	};

	// Custom causes defined within OpenBTS.
	enum CustomCause {
		// We use No_Paging_Response because there is no precise L3Cause for this (closest is DestinationOutOfOrder)
		No_Paging_Response = 1 | LocusCustom,
		IMSI_Detached,		// This is a MAP error code.  If we did not have this we would have to use DestinationOutOfOrder.
		Handover_Outbound,
		Handover_Error,			// Logic error during handover.
		Invalid_Handover_Message,
		Missing_Called_Party_Number,
		Layer2_Error,
		Sip_Internal_Error,
		L3_Internal_Error,
		No_Transaction_Expected,	// Used when we dont expect termination to have any transactions to close out.
		Already_Closed,			// Used when we know for sure the transaction has already been terminated.
		SMS_Timeout,
		SMS_Error,
		SMS_Success,
		USSD_Error,
		USSD_Success,
		MM_Success,
	};

	// Any of the causes described by the Locus above.
	// C++ has no graceful way to do this; enums cannot be inherited or extended.
	union AnyCause {
		CCCause ccCause;
		MMRejectCause mmCause;
		BSSCause bssCause;
		CustomCause customCause;
		int value;
		//AnyCause(CCCause cause) : ccCause(cause) { }
		//AnyCause(MMRejectCause cause) : mmCause(cause) { }
		//AnyCause(BSSCause cause) : bssCause(cause) { }
		//AnyCause(CustomCause cause) : customCause(cause) { }
		AnyCause(int wValue) { value = wValue; }
		AnyCause() : value(0) { }	// initialize to known and unused value.
		bool isEmpty() { return value == 0; }
		Locus getLocus() { return (Locus) (value & 0xff00); }
		Locus getNonLocus() { return (Locus) (value & 0x00ff); }
	};

	static const char *CCCause2Str(CCCause cause);
	static const char *BSSCause2Str(BSSCause cause);
	static const char *CustomCause2Str(CustomCause cause);
	static const char *AnyCause2Str(AnyCause cause);
	static void createCauseMap();	// inits gCauseNameMap, only needs to be called once.
};

typedef L3Cause::AnyCause AnyCause;

typedef L3Cause::CCCause CCCause;
extern void _force_L3Enums_to_load();
extern int CauseName2Cause(std::string causeName);


};	// namespace
#endif
