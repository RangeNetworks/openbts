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

#include "L3Enums.h"
#include <map>
#include <string>
#define CASENAME(x) case x: return #x;

namespace GSM {
using std::string;
using std::map;

std::map<std::string,int> gCauseNameMap;

// The load order in the makefile is such that L3Enums.cpp does not get loaded,
// so force it to load by calling this dummy function from this directory.
void _force_L3Enums_to_load() {}

const char *L3RRCause::RRCause2Str(L3RRCause::RRCause cause)
{
	switch (cause) {
		CASENAME(Normal_Event)
		CASENAME(Unspecified)
		CASENAME(Channel_Unacceptable)
		CASENAME(Timer_Expired)
		CASENAME(No_Activity_On_The_Radio)
		CASENAME(Preemptive_Release)
		CASENAME(UTRAN_Configuration_Unknown)
		CASENAME(Handover_Impossible)
		CASENAME(Channel_Mode_Unacceptable)
		CASENAME(Frequency_Not_Implemented)
		CASENAME(Leaving_Group_Call_Area)
		CASENAME(Lower_Layer_Failure)
		CASENAME(Call_Already_Cleared)
		CASENAME(Semantically_Incorrect_Message)
		CASENAME(Invalid_Mandatory_Information)
		CASENAME(Message_Type_Invalid)
		CASENAME(Message_Type_Not_Compapatible_With_Protocol_State)
		CASENAME(Conditional_IE_Error)
		CASENAME(No_Cell_Available)
		CASENAME(Protocol_Error_Unspecified)
		// Do not add a default case - we want the compiler to whine if a case is undefined...
	} // switch

	return "<unrecognized L3 RR Cause>";
}

const char *L3RejectCause::rejectCause2Str(L3RejectCause::RejectCause cause)
{
	switch (cause) {
		CASENAME(Zero)
		CASENAME(IMSI_Unknown_In_HLR)
		CASENAME(Illegal_MS)
		CASENAME(IMSI_Unknown_In_VLR)
		CASENAME(IMEI_Not_Accepted)
		CASENAME(Illegal_ME)
		CASENAME(PLMN_Not_Allowed)
		CASENAME(Location_Area_Not_Allowed)
		CASENAME(Roaming_Not_Allowed_In_LA)
		CASENAME(No_Suitable_Cells_In_LA)
		CASENAME(Network_Failure)
		CASENAME(MAC_Failure)
		CASENAME(Synch_Failure)
		CASENAME(Congestion)
		CASENAME(GSM_Authentication_Unacceptable)
		CASENAME(Not_Authorized_In_CSG)
		CASENAME(Service_Option_Not_Supported)
		CASENAME(Requested_Service_Option_Not_Subscribed)
		CASENAME(Service_Option_Temporarily_Out_Of_Order)
		CASENAME(Call_Cannot_Be_Identified)
		CASENAME(Semantically_Incorrect_Message)
		CASENAME(Invalid_Mandatory_Information)
		CASENAME(Message_Type_Invalid)
		CASENAME(Message_Type_Not_Compatible_With_Protocol_State)
		CASENAME(IE_Invalid)
		CASENAME(Conditional_IE_Error)
		CASENAME(Message_Not_Compatible_With_Protocol_State)
		CASENAME(Protocol_Error_Unspecified)
		// Do not add a default case - we want the compiler to whine if a case is undefined...
	} // switch

	return "<unrecognized L3 Reject Cause>";
}

const char *L3Cause::CCCause2Str(CCCause cause)
{
	switch (cause) {
		CASENAME(Unknown_L3_Cause)
		CASENAME(Unassigned_Number)
		CASENAME(No_Route_To_Destination)
		CASENAME(Channel_Unacceptable)
		CASENAME(Operator_Determined_Barring)
		CASENAME(Normal_Call_Clearing)
		CASENAME(User_Busy)
		CASENAME(No_User_Responding)
		CASENAME(User_Alerting_No_Answer)
		CASENAME(Call_Rejected)
		CASENAME(Number_Changed)
		CASENAME(Preemption)
		CASENAME(Non_Selected_User_Clearing)
		CASENAME(Destination_Out_Of_Order)
		CASENAME(Invalid_Number_Format)
		CASENAME(Facility_Rejected)
		CASENAME(Response_To_STATUS_ENQUIRY)
		CASENAME(Normal_Unspecified)
		CASENAME(No_Channel_Available)
		CASENAME(Network_Out_Of_Order)
		CASENAME(Temporary_Failure)
		CASENAME(Switching_Equipment_Congestion)
		CASENAME(Access_Information_Discarded)
		CASENAME(Requested_Channel_Not_Available)
		CASENAME(Resources_Unavailable)
		CASENAME(Quality_Of_Service_Unavailable)
		CASENAME(Requested_Facility_Not_Subscribed)
		CASENAME(Incoming_Calls_Barred_Within_CUG)
		CASENAME(Bearer_Capability_Not_Authorized)
		CASENAME(Bearer_Capability_Not_Presently_Available)
		CASENAME(Service_Or_Option_Not_Available)
		CASENAME(Bearer_Service_Not_Implemented)
		CASENAME(ACM_GE_Max)
		CASENAME(Requested_Facility_Not_Implemented)
		CASENAME(Only_Restricted_Digital_Information_Bearer_Capability_Is_Available)
		CASENAME(Service_Or_Option_Not_Implemented)
		CASENAME(Invalid_Transaction_Identifier_Value)
		CASENAME(User_Not_Member_Of_CUG)
		CASENAME(Incompatible_Destination)
		CASENAME(Invalid_Transit_Network_Selection)
		CASENAME(Semantically_Incorrect_Message)
		CASENAME(Invalid_Mandatory_Information)
		CASENAME(Message_Type_Not_Implemented)
		CASENAME(Messagetype_Not_Compatible_With_Protocol_State)
		CASENAME(IE_Not_Implemented)
		CASENAME(Conditional_IE_Error)
		CASENAME(Message_Not_Compatible_With_Protocol_State)
		CASENAME(Recovery_On_Timer_Expiry)
		CASENAME(Protocol_Error_Unspecified)
		CASENAME(Interworking_Unspecified)
		// Do not add a default case - we want the compiler to whine if a case is undefined...
	} // switch

	return "<unrecognized Call Control Cause>";
}

const char *L3Cause::BSSCause2Str(L3Cause::BSSCause cause)
{
	switch (cause) {
		CASENAME(Radio_Interface_Failure)
		CASENAME(Uplink_Quality)
		CASENAME(Uplink_Strength)
		CASENAME(Downlink_Quality)
		CASENAME(Downlink_Strength)
		CASENAME(Distance)
		CASENAME(Operator_Intervention)
		CASENAME(Channel_Assignment_Failure)
		CASENAME(Handover_Successful)
		CASENAME(Better_Cell)
		CASENAME(Traffic)
		CASENAME(Reduce_Load_In_Serving_Cell)
		CASENAME(Traffic_Load)
		CASENAME(Traffic_Load_In_Target_Cell_Higher_Than_In_Source_Cell)
		CASENAME(Relocation_Triggered)
		CASENAME(Equipment_Failure)
		CASENAME(No_Radio_Resource_Available)
		CASENAME(CCCH_Overload)
		CASENAME(Processor_Overload)
		CASENAME(Emergency_Preemption)
		CASENAME(DTM_Handover_SGSN_Failure)
		CASENAME(DTM_Handover_PS_Allocation_Failure)
		CASENAME(Transcoding_Mismatch)
		CASENAME(Requested_Speech_Version_Unavailable)
		CASENAME(Ciphering_Algorithm_Not_Supported)
		// Do not add a default case - we want the compiler to whine if a case is undefined...
	};
	return "<unrecognized BSS Cause>";
}

const char *L3Cause::CustomCause2Str(L3Cause::CustomCause cause)
{
	switch (cause) {
		CASENAME(No_Paging_Response)
		CASENAME(IMSI_Detached)
		CASENAME(Handover_Outbound)
		CASENAME(Handover_Error)
		CASENAME(Invalid_Handover_Message)
		CASENAME(Missing_Called_Party_Number)
		CASENAME(Layer2_Error)
		CASENAME(Sip_Internal_Error)
		CASENAME(L3_Internal_Error)
		CASENAME(No_Transaction_Expected)
		CASENAME(Already_Closed)
		CASENAME(SMS_Timeout)
		CASENAME(SMS_Error)
		CASENAME(SMS_Success)
		CASENAME(USSD_Error)
		CASENAME(USSD_Success)
		CASENAME(MM_Success)
		// Do not add a default case - we want the compiler to whine if a case is undefined...
	};
	return "<unrecognized Custom Cause>";
}

const char *L3Cause::AnyCause2Str(AnyCause cause)
{
	switch (cause.getLocus()) {
		case LocusCC: return CCCause2Str(cause.ccCause);
		case LocusMM: return L3RejectCause::rejectCause2Str((L3RejectCause::RejectCause) cause.getNonLocus());
		case LocusBSS: return BSSCause2Str((BSSCause) cause.value);
		case LocusCustom: return CustomCause2Str((CustomCause) cause.value);
	}
	return "<unrecognized cause>";
}

void L3Cause::createCauseMap()
{
	if (gCauseNameMap.size()) { return; }	// Created previously.
	AnyCause acause;
	for (acause.value = 0; acause.value < LocusCustom+0xff; acause.value++) {
		const char *name = AnyCause2Str(acause);
		if (name[0] != '<') {
			gCauseNameMap[string(name)] = acause.value;
		}
	}
}

// Accept cause name, identical as used in the enums, of any type of cause (except RR) and return the AnyCause value,
// which means the high byte is the Locus and the low byte is the cause.
// Return 0 if not found.
int CauseName2Cause(string causeName)
{
	if (gCauseNameMap.size() == 0) { L3Cause::createCauseMap(); }
	map<string,int>::const_iterator it = gCauseNameMap.find(causeName);
	if (it != gCauseNameMap.end()) {
		return it->second;
	} else {
		return 0;
	}
}

};
