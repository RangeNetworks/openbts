/*
* Copyright 2011 Range Networks, Inc.
* All Rights Reserved.
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

#ifndef SGSNBASE_H
#define SGSNBASE_H
#include "Logger.h"
#include "miniggsn.h"

#define SNDCP_IN_PDP 1

namespace SGSN {
#ifdef WARNING
#undef WARNING
#endif

extern bool sgsnDebug();
//#define GGSNLOG2(level,who,stuff) {std::cout <<who<<stuff<<"\n";}
// Normal log message, but if debugging is on print all regardless of Log.Level.
//#define GGSNLOG1(level,who,stuff) if (IS_LOG_LEVEL(level)||sgsnDebug()) _LOG(level)<<who<<timestr()<<","<<stuff;
#define GGSNLOG1(level,who,stuff) if (IS_LOG_LEVEL(level)||sgsnDebug()) _LOG(level)<<who<<stuff;
// Normal log plus put it in the ggsn.log file.
#define GGSNLOG2(level,who,stuff) GGSNLOG1(level,who,stuff); \
		if (sgsnDebug()) MGLOG(who<<stuff);
#define GGSNLOG(stuff) GGSNLOG2(INFO,"SGSN:",stuff)
#define LLCDEBUG(stuff)   GGSNLOG1(DEBUG,"LLC:",stuff)
#define SNDCPDEBUG(stuff) GGSNLOG1(DEBUG,"SNDCP:",stuff)
#define LLCWARN(stuff) GGSNLOG2(WARNING,"LLC:",stuff)
#define LLCINFO(stuff) GGSNLOG2(INFO,"LLC:",stuff)
#define SGSNLOG(stuff)  GGSNLOG2(INFO,"SGSN:",stuff)
#define SGSNERROR(stuff)  GGSNLOG2(ERR,"SGSN:",stuff)
#define SGSNWARN(stuff)  GGSNLOG2(WARNING,"SGSN:",stuff)
//#define SGSNDEBUG(stuff)  GGSNLOG1(DEBUG,"SGSN:",stuff) not used

class LlcEngine;
class LlcEntity;
class LlcEntityUserData;
class LlcEntityGmm;
class Sndcp;
class SgsnInfo;
class Sgsn;
class PdpContext;
class SgsnError {};	// Target for throw/catch.

// 24.008 10.5.5.14 GMM Cause - see also SmCause
struct GmmCause {
	enum Cause {
		IMSI_unknown_in_HLR = 2,
		Illegal_MS = 3,
		IMEI_not_accepted = 5,
		Illegal_ME = 6,
		GPRS_services_not_allowed = 7,
		GPRS_services_and_non_GPRS_services_not_allowed = 8,
		MS_identity_cannot_be_derived_by_the_network = 9,
		Implicitly_detached = 0xa,
		PLMN_not_allowed = 0xb,
		Location_Area_not_allowed = 0xc,
		Roaming_not_allowed_in_this_location_area = 0xd,
		GPRS_services_not_allowed_in_this_PLMN = 0xe,
		No_Suitable_Cells_In_Location_Area = 0xf,
		MSC_temporarily_not_reachable = 0x10,
		Network_failure = 0x11,
		MAC_failure = 0x14,
		Synch_failure = 0x15,
		Congestion = 0x16,
		GSM_authentication_unacceptable = 0x17,
		Not_authorized_for_this_CSG = 0x19,
		No_PDP_context_activated = 0x28,
		// 0x30 to 0x3f - retry upon entry into a new cell?
		Semantically_incorrect_message = 0x5f,
		Invalid_mandatory_information = 0x60,
		Message_type_nonexistent_or_not_implemented = 0x61,
		Message_type_not_compatible_with_the_protocol_state = 0x62,
		Information_element_nonexistent_or_not_implemented = 0x63,
		Conditional_IE_error = 0x64,
		Message_not_compatible_with_the_protocol_state = 0x65,
		Protocol_error_unspecified = 0x6f
	};
	static const char *name(unsigned mt, bool ornull=0);
};


// 24.008 10.5.5.6 SM Cause - see also GmmCause
struct SmCause {
	enum Cause {
	/*0000,1000*/ Operator_Determined_Barring = 0x08,
	/*0001,1000*/ MBMS_bearer_capabilities_insufficient_for_the_service = 0x18,
	/*0001,1001*/ LLC_or_SNDCP_failure = 0x19,	// A/Gb mode only
	/*0001,1010*/ Insufficient_resources = 0x1a,
	/*0001,1011*/ Missing_or_unknown_APN = 0x1b,
	/*0001,1100*/ Unknown_PDP_address_or_PDP_type = 0x1c,
	/*0001,1101*/ User_authentication_failed = 0x1d,
	/*0001,1110*/ Activation_rejected_by_GGSN_Serving_GW_or_PDN_GW = 0x1e,
	/*0001,1111*/ Activation_rejected_unspecified = 0x1f,
	/*0010,0000*/ Service_option_not_supported = 0x20,
	/*0010,0001*/ Requested_service_option_not_subscribed = 0x21,
	/*0010,0010*/ Service_option_temporarily_out_of_order = 0x22,
	/*0010,0011*/ NSAPI_already_used = 0x23,	// (not sent)
	/*0010,0100*/ Regular_deactivation = 0x24,
	/*0010,0101*/ QoS_not_accepted = 0x25,
	/*0010,0110*/ Network_failure = 0x26,
	/*0010,0111*/ Reactivation_required = 0x27, 
	/*0010,1000*/ Feature_not_supported = 0x28,
	/*0010,1001*/ Semantic_error_in_the_TFT_operation = 0x29,
	/*0010,1010*/ Syntactical_error_in_the_TFT_operation = 0x2a,
	/*0010,1011*/ Unknown_PDP_context = 0x2b,
	/*0010,1100*/ Semantic_errors_in_packet_filter = 0x2c,
	/*0010,1101*/ Syntactical_errors_in_packet_filter = 0x2d,
	/*0010,1110*/ PDP_context_without_TFT_already_activated = 0x2e,
	/*0010,1111*/ Multicast_group_membership_timeout = 0x2f,
	/*0011,0000*/ Activation_rejected_BCM_violation = 0x30,
	/*0011,0010*/ PDP_type_IPv4_only_allowed = 0x32,
	/*0011,0011*/ PDP_type_IPv6_only_allowed = 0x33,
	/*0011,0100*/ Single_address_bearers_only_allowed = 0x34,
	/*0011,1000*/ Collision_with_network_initiated_request = 0x38,
	/*0101,0001*/ Invalid_transaction_identifier_value = 0x51,
	/*0101,1111*/ Semantically_incorrect_message = 0x5f,
	/*0110,0000*/ Invalid_mandatory_information = 0x60,
	/*0110,0001*/ Message_type_nonexistent_or_not_implemented = 0x61,
	/*0110,0010*/ Message_type_not_compatible_with_the_protocol_state = 0x62,
	/*0110,0011*/ Information_element_nonexistent_or_not_implemented = 0x63,
	/*0110,0100*/ Conditional_IE_error = 0x64,
	/*0110,0101*/ Message_not_compatible_with_the_protocol_state = 0x65,
	/*0110,1111*/ Protocol_error_unspecified = 0x6f,
	/*0111,0000*/ APN_restriction_value_incompatible_with_active_PDP_context = 0xf0
	};
	static const char *name(unsigned mt, bool ornull = 0);
};
typedef SmCause::Cause SmCauseType;

};
#endif
