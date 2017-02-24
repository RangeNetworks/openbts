/*
* Copyright 2008, 2009, 2010 Free Software Foundation, Inc.
* Copyright 2010 Kestrel Signal Processing, Inc.
* Copyright 2011, 2012, 2013, 2014 Range Networks, Inc.
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

#include <iostream>
#include <fstream>
#include <vector>
#include <string>

#include <Globals.h>
#include "OpenBTSConfig.h"

std::string getARFCNsString(unsigned band) {
	std::stringstream ss;
	int i;
	float downlink;
	float uplink;

	if (band == 850) {
		// 128:251 GSM850
		downlink = 869.2;
		uplink = 824.2;
		for (i = 128; i <= 251; i++) {
			ss << i << "|GSM850 #" << i << " : " << downlink << " MHz downlink / " << uplink << " MHz uplink,";
			downlink += 0.2;
			uplink += 0.2;
		}

	} else if (band == 900) {
		// 0:124 PGSM900
		downlink = 935.2;
		uplink = 890.2;
		for (i = 0; i <= 124; i++) {
			ss << i << "|PGSM900 #" << i << " : " << downlink << " MHz downlink / " << uplink << " MHz uplink,";
			downlink += 0.2;
			uplink += 0.2;
		}

		// 975:1023 EGSM900
		downlink = 1130;
		uplink = 1085;
		for (i = 975; i <= 1023; i++) {
			ss << i << "|EGSM900 #" << i << " : " << downlink << " MHz downlink / " << uplink << " MHz uplink,";
			downlink += 0.2;
			uplink += 0.2;
		}

	} else if (band == 1800) {
		// 512:885 DCS1800
		downlink = 1805.2;
		uplink = 1710.2;
		for (i = 512; i <= 885; i++) {
			ss << i << "|DCS1800 #" << i << " : " << downlink << " MHz downlink / " << uplink << " MHz uplink,";
			downlink += 0.2;
			uplink += 0.2;
		}

	} else if (band == 1900) {
		// 512:810 PCS1900
		downlink = 1930.2;
		uplink = 1850.2;
		for (i = 512; i <= 810; i++) {
			ss << i << "|PCS1900 #" << i << " : " << downlink << " MHz downlink / " << uplink << " MHz uplink,";
			downlink += 0.2;
			uplink += 0.2;
		}
	}

	std::string tmp = ss.str();
	return tmp.substr(0, tmp.size()-1);
}

// The old handover config keys that have disappeared include:
// (pat) The LocalRSSIMin option was misnamed.  It checks RXLEV reported by the MS, which is not RSSI.
// 	"GSM.Handover.ThresholdDelta","10", 		// Closest new option is GSM.Handover.Target
//	"GSM.Handover.LocalRSSIMin","-80"			// Closest new option is GSM.Handover.RXLEV_DL.Margin
// 	"GSM.Handover.Averaging","5",				// Closest new option is GSM.Handover.RXLEV_DL.History
// 	"GSM.Ny1",									// Renamed to: GSM.Handover.Ny1
static void makeHandoverKeys(ConfigurationKeyMap &map)
{
#define RXDIFF_HELP

	{ ConfigurationKey tmp("GSM.Handover.FailureHoldoff","20",	// (pat 3-2014) Increased from 5.
		"seconds",
		ConfigurationKey::CUSTOMERTUNE,
		ConfigurationKey::VALRANGE,
		"1:9999",
		false,
		"The number of seconds to wait before attempting another handover with a given neighbor BTS."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("GSM.Handover.Margin","15",
		"db",
		ConfigurationKey::CUSTOMERTUNE,
		ConfigurationKey::VALRANGE,
		"1:100",
		true,
		"Unconditional handover if RXDIFF exceeds this margin.  "
		"The GSM.Handover.RXLEV_DL.PenaltyTime will prevent reverse handovers for that period.  "
		RXDIFF_HELP
	);
	map[tmp.getName()] = tmp;
	}

	// (pat) 5 is way too low; the MS does not have to respond.  There is no penalty for making this too big, so I am increasing it a lot.
	{ ConfigurationKey tmp("GSM.Handover.Ny1","50",
		"repeats",
		ConfigurationKey::CUSTOMERTUNE,
		ConfigurationKey::VALRANGE,
		"1:200",
		true,
		"Maximum number of repeats of the Physical Information Message during handover procedure, GSM 04.08 11.1.3."
	);
	map[tmp.getName()] = tmp;
	}

	// (pat) There used to be a Handover.History.Min, but it was replaced by individual history counts for each handover parameter.
	{ ConfigurationKey tmp("GSM.Handover.History.Max","32",
		"reports",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::VALRANGE,
		"2:128",
		false,
		"Maximum neighbor history to consider for handover.  "
		"Units are number of measurement reports, which occur once each 480ms.  "
	);
	map[tmp.getName()] = tmp;
	}


	// (pat) All these handover keys are almost identical, so simplify a bit...
	struct HandoverKey : public ConfigurationKey {
		char namebuf[100];
		char defaultValueBuf[40];

		HandoverKey(ConfigurationKeyMap &map,const char *name, int defaultValue, const char *units, const char *range, const char *help = "")
		{
			snprintf(namebuf,sizeof(namebuf),"GSM.Handover.%s",name);
			snprintf(defaultValueBuf,sizeof(defaultValueBuf),"%d",defaultValue);
			ConfigurationKey tmp(namebuf,
				defaultValueBuf,
				units,
				ConfigurationKey::DEVELOPER,
				ConfigurationKey::VALRANGE,
				range,
				false,
				help);
			map[namebuf] = tmp;
		}
	};

	const char *RXLEV_Help = "RXLEV_DL is reported by the MS for the serving cell and each neighbor cell.  "
		"Handover attempted if the serving cell RXLEV_DL < GSM.Handover.RXLEV_DL.Target and RXDIFF > GSM.Handover.RXLEV_DL.Margin";
	const char *History_Help = "The number of 480ms periods to consider for this handover criteria.";
	const char *PenaltyTime_Help = "After a handover a reverse handover to the originating BTS is prevented for this period of time in seconds. ";

	HandoverKey(map, "RXLEV_DL.Target",  60, "dB", "0:100", RXLEV_Help);
	HandoverKey(map, "RXLEV_DL.History", 6, "periods", "2:32",  History_Help);
	HandoverKey(map, "RXLEV_DL.Margin",   10, "dB", "0:100", RXLEV_Help);
	HandoverKey(map, "RXLEV_DL.PenaltyTime",  20, "seconds", "0:99999", PenaltyTime_Help);

}

ConfigurationKeyMap getConfigurationKeys()
{

	//VALIDVALUES NOTATION:
	// (pat) for ConfigurationKey::CHOICE and CHOICE_OPT:
	// * A:B : range from A to B in steps of 1
	// * A:B(C) : range from A to B in steps of C
	// * A:B,D:E : range from A to B and from D to E
	// * A,B : multiple choices of value A and B
	// * X|A,Y|B : multiple choices of string "A" with value X and string "B" with value Y
	// For CHOICE_OPT, the input value can also be empty.
	// 
	// (pat) I believe the following is valid for ConfigurationKey::REGEX and REGEX_OPT
	// * ^REGEX$ : string must match regular expression
	// For REGEX_OPT, the input value can also be empty.
	//
	// (pat) for ConfigurationKey::VALRANGE looks like the syntax is this subset:
	// 		A:B : range from A to B in steps of 1
	// 		A:B(C) : range from A to B in steps of C, but C is ignored.
	// 		and a comma is silently ignored?
	// 		The input value is constrained to be a number.

	/*
	TODO : double check: sometimes symbol periods == 0.55km and other times 1.1km?
	TODO : .defines() vs sql audit
	TODO : Optional vs *_OPT audit
	TODO : configGetNumQ()
	TODO : description contains "if specified" == key is optional
	TODO : crossCheck possibility here: GSM.CellOptions.RADIO-LINK-TIMEOUT should be coordinated with T3109.
	TODO : These things exist but aren't defined as keys.
			- GSM.SI3RO
			- GSM.SI3RO.CBQ
			- GSM.SI3RO.CRO
			- GSM.SI3RO.TEMPORARY_OFFSET
			- GSM.SI3RO.PENALTY_TIME
			- GPRS.SGSN.External
			- GPRS.SGSN.Host
			- 'GPRS.TBF.Downlink.NStuck','250',0,0,'Maximum number of blocks sent to non-advancing TBF'
			- 'GPRS.MS.ResponseTime','4',0,0,'Allow the MS this much processing time to respond to an assignment message, specified as a count of RLC blocks.  The MS must send its response this many blocks after receiving the message.  Can also add an arbitrary additional delay if necessary.'
			- ...
	*/

	ConfigurationKeyMap map;

	makeHandoverKeys(map);

	{ ConfigurationKey tmp("Core.File","core.openbts",
		"",
		ConfigurationKey::FACTORY,
		ConfigurationKey::STRING,
		"",
		false,
		"Constant part of core file name to use (excluding optional pid)"
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("Core.Pid","0",
		"",
		ConfigurationKey::FACTORY,
		ConfigurationKey::BOOLEAN,
		"",
		false,
		"1 to add a .pid number to the end of the filename"
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("Core.SaveFiles","1",
		"",
		ConfigurationKey::FACTORY,
		ConfigurationKey::BOOLEAN,
		"",
		false,
		"1 to save system files in a tarball for post-mortem analysis"
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("Core.TarFile","/tmp/openbtsfiles.tgz",
		"",
		ConfigurationKey::FACTORY,
		ConfigurationKey::STRING,
		"",
		false,
		"Name of filename to save /proc files for post-mortem analysis after a crash"
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("CLI.Port","49300",
		"",
		ConfigurationKey::FACTORY,
		ConfigurationKey::PORT,
		"",
		false,
		"Port number (tcp/udp) for use in communicating between CLI and OpenBTS"
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("CLI.Interface","127.0.0.1",
		"",
		ConfigurationKey::FACTORY,
		ConfigurationKey::IPADDRESS,
		"",
		false,
		"Interface for use in communicating between CLI and OpenBTS, use \"any\" for all interfaces, otherwise, a comma separated list of interfaces"
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("Control.Call.QueryRRLP.Early","0",
		"",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::BOOLEAN,
		"",
		false,
		"Query every MS for its location via RRLP during the setup of a call."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("Control.Call.QueryRRLP.Late","0",
		"",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::BOOLEAN,
		"",
		false,
		"Query every MS for its location via RRLP during the teardown of a call."
	);
	map[tmp.getName()] = tmp;
	}


	{ ConfigurationKey tmp("Control.GSMTAP.GPRS","0",
		"",
		ConfigurationKey::CUSTOMERWARN,
		ConfigurationKey::BOOLEAN,
		"",
		false,
		"Capture GPRS signaling and traffic at L1/L2 interface via GSMTAP."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("Control.GSMTAP.GSM","0",
		"",
		ConfigurationKey::CUSTOMERWARN,
		ConfigurationKey::BOOLEAN,
		"",
		false,
		"Capture GSM signaling at L1/L2 interface via GSMTAP."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("Control.GSMTAP.TargetIP","127.0.0.1",
		"",
		ConfigurationKey::CUSTOMERWARN,
		ConfigurationKey::IPADDRESS,
		"",
		false,
		"Target IP address for GSMTAP packets; the IP address of Wireshark, if you use it for real time traces."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("Control.LUR.AttachDetach","1",
		"",
		ConfigurationKey::CUSTOMERWARN,		// (pat) We have never tested with AttachDetach == 0; so customers should not use it!
		ConfigurationKey::BOOLEAN,
		"",
		false,
		"Use attach/detach procedure.  "
			"This will make initial LUR more prompt.  "
			"It will also cause an un-registration if the handset powers off and really heavy LUR loads in areas with spotty coverage.  "
			"Range Networks strongly recommends setting this to 1.  "	// (pat) added, until someone tests this!
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("Control.LUR.RegistrationMessageFrequency","FIRST",
		"^PLMN|NORMAL|FIRST$",
		ConfigurationKey::CUSTOMERWARN,
		ConfigurationKey::STRING,
		"",
		false,
		"This option helps determine when a registration message is sent by the BTS to a handset.  "
		"If 'PLMN' the message is sent only when the handset first registers in the PLMN, as reported by the handset. "
		"If 'NORMAL' the message is sent whenever the handset enters the cell, as reported by the handset. "
		"If 'FIRST' the message is sent the first time this BTS sees this MS as determined by the WELCOME_SENT field of the TMSI_TABLE."
		"This option is not completely reliable because the functioning of this option depends on information provided "
		"by the handset during their initial attach procedure, and some handsets set this information improperly."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("Control.LUR.FailedRegistration.Message","Your handset is not provisioned for this network. ",
		"",
		ConfigurationKey::CUSTOMER,
		ConfigurationKey::STRING_OPT,// audited
		"^[ -~]+$",
		false,
		"Send this text message, followed by the IMSI, to unprovisioned handsets that are denied registration."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("Control.LUR.FailedRegistration.ShortCode","1000",
		"",
		ConfigurationKey::CUSTOMER,
		ConfigurationKey::STRING,
		"^[0-9]+$",
		false,
		"The return address for the failed registration message.  "
		"If unset, the message will not be sent.  "
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("Control.LUR.NormalRegistration.Message","",
		"",
		ConfigurationKey::CUSTOMER,
		ConfigurationKey::STRING_OPT,// audited
		"^[ -~]+$",
		false,
		"The text message, followed by the IMSI, to be sent to provisioned handsets when they attach on Um.  "
			"By default, no message is sent.  "
			"To have a message sent, specify one.  "
			"To stop sending messages again, execute \"unconfig Control.LUR.NormalRegistration.Message\".  "
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("Control.LUR.NormalRegistration.ShortCode","0000",
		"",
		ConfigurationKey::CUSTOMER,
		ConfigurationKey::STRING,
		"^[0-9]+$",
		false,
		"The return address for the normal registration message.  "
		"If unset, the message will not be sent.  "
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("Control.LUR.OpenRegistration","",
		"",
		ConfigurationKey::CUSTOMERWARN,
		ConfigurationKey::REGEX_OPT,// audited
		"",
		false,
		"This value is a regular expression.  "
			"Any handset with an IMSI matching the regular expression is allowed to register, even if it is not provisioned.  "
			"By default, this feature is disabled.  "
			"To enable open registration, specify a regular expression to match.  E.g. ^001, which matches any IMSI starting with 001, the MCC for test networks.  "
			"To disable open registration again, execute \"unconfig Control.LUR.OpenRegistration\"."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("Control.LUR.OpenRegistration.Message","Welcome to the test network.  Your IMSI is ",
		"",
		ConfigurationKey::CUSTOMER,
		ConfigurationKey::STRING,
		"^[ -~]+$",
		false,
		"Send this text message, followed by the IMSI, to unprovisioned handsets when they attach on Um due to open registration."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("Control.LUR.OpenRegistration.Reject","",
		"",
		ConfigurationKey::CUSTOMERWARN,
		ConfigurationKey::REGEX_OPT,// audited
		"",
		false,
		"This value is a regular expression.  "
			"Any unprovisioned handset with an IMSI matching the regular expression is rejected for registration, even if it matches Control.LUR.OpenRegistration.  "
			"By default, this filter is disabled.  "
			"To enable the filter, specify a regular expression.  E.g. ^666 matches any IMSI starting with 666, which currently does not correspond to any known MCC.  Stay on the light side of the Force!"
			"To disable the filter again, execute \"unconfig Control.LUR.OpenRegistration.Reject\".  "
			"If Control.LUR.OpenRegistration is disabled, this parameter has no effect."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("Control.LUR.OpenRegistration.ShortCode","101",
		"",
		ConfigurationKey::CUSTOMER,
		ConfigurationKey::STRING,
		"^[0-9]+$",
		false,
		"The return address for the open registration message."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("Control.LUR.QueryClassmark","0",
		"",
		ConfigurationKey::CUSTOMER,
		ConfigurationKey::BOOLEAN,
		"",
		false,
		"Query every MS for classmark during LUR."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("Control.LUR.QueryIMEI","0",
		"",
		ConfigurationKey::CUSTOMER,
		ConfigurationKey::BOOLEAN,
		"",
		false,
		"Query every MS for IMEI during initial LUR."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("Control.LUR.QueryRRLP","0",
		"",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::BOOLEAN,
		"",
		false,
		"Query every MS for its location via RRLP during LUR."
	);
	map[tmp.getName()] = tmp;
	}


	{ ConfigurationKey tmp("Control.LUR.SendTMSIs","0",
		"",
		ConfigurationKey::CUSTOMER,
		ConfigurationKey::BOOLEAN,
		"",
		false,
		"Send new TMSI assignments to handsets that are allowed to attach."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("Control.LUR.FailMode","ACCEPT",
		"",	// no units
		ConfigurationKey::CUSTOMER,
		ConfigurationKey::CHOICE,
		"ACCEPT,FAIL,OPEN",
		false,	// not static
		"Action to take after registration failure due to network failure, error in Registrar, or other unexpected error.  "
		"This does not apply to regular authorization failure handled by other config options.  "
		"If ACCEPT the handset is authorized for service.  If FAIL the handset is denied service.  If OPEN the open registration procedure is applied."
		);
	map[tmp.getName()] = tmp;
	}


	// (pat) 6-2013:  If you send cause==4, the MS continues to retry.
	// Cause==3 and 6 are commented out because:
	// "David constantly stressed them being very disruptive to out-of-network phones.
	// "You're not just saying "go away from me" you're saying "this phone has been stolen and should now cease to operate until you restart it."
	{ ConfigurationKey tmp("Control.LUR.UnprovisionedRejectCause","0x04",
		"",
		ConfigurationKey::CUSTOMERWARN,
		ConfigurationKey::CHOICE,
		"0x02|IMSI unknown in HLR,"
			//"0x03|Illegal MS,"
			"0x04|IMSI unknown in VLR,"
			"0x05|IMEI not accepted,"
			//"0x06|Illegal ME,"
			"0x0B|PLMN not allowed,"
			"0x0C|Location Area not allowed,"
			"0x0D|Roaming not allowed in this location area,"
			"0x11|Network failure,"
			"0x16|Congestion,"
			"0x20|Service option not supported,"
			"0x21|Requested service option not subscribed,"
			"0x22|Service option temporarily out of order,"
			"0x26|Call cannot be identified,"
			"0x30|Retry upon entry into a new cell,"
			"0x5F|Semantically incorrect message,"
			"0x60|Invalid mandatory information,"
			"0x61|Message type non-existent or not implemented,"
			"0x62|Message type not compatible with the protocol state,"
			"0x63|Information element non-existent or not implemented,"
			"0x64|Conditional IE error,"
			"0x65|Message not compatible with the protocol state,"
			"0x6F|Unspecified protocol error",
		false,
		"Reject cause for location updating failures for unprovisioned phones, that is, the IMSI was not found in the Registrar database.  "
			"The SIP result code from the Registrar in this case is 401.  "
			"Reject causes come from GSM 04.08 10.5.3.6.  "
			"Reject cause 0x02 or 0x04 is usually the right one."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("Control.LUR.404RejectCause","0x04",
		"",
		ConfigurationKey::CUSTOMERWARN,
		ConfigurationKey::CHOICE,
		"0x02|IMSI unknown in HLR,"
			//"0x03|Illegal MS,"
			"0x04|IMSI unknown in VLR,"
			"0x05|IMEI not accepted,"
			//"0x06|Illegal ME,"
			"0x0B|PLMN not allowed,"
			"0x0C|Location Area not allowed,"
			"0x0D|Roaming not allowed in this location area,"
			"0x11|Network failure,"
			"0x16|Congestion,"
			"0x20|Service option not supported,"
			"0x21|Requested service option not subscribed,"
			"0x22|Service option temporarily out of order,"
			"0x26|Call cannot be identified,"
			"0x30|Retry upon entry into a new cell,"
			"0x5F|Semantically incorrect message,"
			"0x60|Invalid mandatory information,"
			"0x61|Message type non-existent or not implemented,"
			"0x62|Message type not compatible with the protocol state,"
			"0x63|Information element non-existent or not implemented,"
			"0x64|Conditional IE error,"
			"0x65|Message not compatible with the protocol state,"
			"0x6F|Unspecified protocol error",
		false,
		"Reject cause for location updating failures for phones that fail authentication.  "
			"The SIP result code from the Registrar in this case is 404.  "
			"Reject causes come from GSM 04.08 10.5.3.6.  "
			"Reject cause 0x02 or 0x04 is usually the right one."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("Control.LUR.TestMode","0",
		"",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::VALRANGE,
		"0:1",
		false,
		"Used for testing the LUR procedure."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("Control.NumSQLTries","3",
		"attempts",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::VALRANGE,
		"1:10",// educated guess
		false,
		"Number of times to retry SQL queries before declaring a database access failure."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("Control.Reporting.PhysStatusTable","/var/run/ChannelTable.db",
		"",
		ConfigurationKey::CUSTOMERWARN,
		ConfigurationKey::FILEPATH,
		"",
		true,
		"File path for channel status reporting database."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("Control.Reporting.StatsTable","/var/log/OpenBTSStats.db",
		"",
		ConfigurationKey::CUSTOMERWARN,
		ConfigurationKey::FILEPATH,
		"",
		true,
		"File path for statistics reporting database."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("Control.Reporting.TMSITable","/var/run/TMSITable.db",
		"",
		ConfigurationKey::CUSTOMERWARN,
		ConfigurationKey::FILEPATH,
		"",
		true,
		"File path for TMSITable database."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("Control.TMSITable.MaxAge","576",
		"",
		ConfigurationKey::CUSTOMERWARN,
		ConfigurationKey::VALRANGE,
		"1:999999999",
		false,	// is static?  Right now this is only done at startup so its kind of static.
		"Maximum allowed age in hours for a TMSI entry in the TMSITable.  "
		"This is not the authorization/registration expiry period, this is how long the BTS remembers assigned TMSIs.  "
		"Currently old entries are only discarded at startup.  "
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("Control.Reporting.TransactionMaxCompletedRecords","100",
		"record",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::VALRANGE,
		"1:10000",
		true,
		"Maximum completed records to be stored for gathering by an external stats tool."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("Control.Reporting.TransactionTable","/var/run/TransactionTable.db",
		"",
		ConfigurationKey::CUSTOMERWARN,
		ConfigurationKey::FILEPATH,
		"",
		true,
		"File path for transaction table database."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("Control.SACCHTimeout.BumpDown","1",
		"dB",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::VALRANGE,
		"1:3",// educated guess
		false,
		"Decrease the RSSI by this amount to induce more power in the MS each time we fail to receive a response from it on SACCH."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("Control.SMS.QueryRRLP","0",
		"",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::BOOLEAN,
		"",
		false,
		"Query every MS for its location via RRLP during an SMS."
	);
	map[tmp.getName()] = tmp;
	}

	// It is CBS [Cell Broadcast Service].
	{ ConfigurationKey tmp("Control.SMSCB.Table","",
		"",
		ConfigurationKey::CUSTOMERWARN,
		ConfigurationKey::FILEPATH_OPT,// audited
		"",
		true,	// static because the channel is started in GSMConfig at init time.
		"File path for CBS [Cell Broadcast Service] scheduling database.  "
			"By default, this feature is disabled.  "
			"To enable, specify a file path for the database e.g. /var/run/SMSCB.db.  "
			"To disable again, execute \"unconfig Control.SMSCB.Table\"."
	);
	map[tmp.getName()] = tmp;
	}


	{ ConfigurationKey tmp("Control.VEA","0",
		"",
		ConfigurationKey::CUSTOMER,
		ConfigurationKey::BOOLEAN,
		"",
		false,
		"Use very early assignment for speech call establishment.  "
			"See GSM 04.08 Section 7.3.2 for a detailed explanation of assignment types.  "
			"If VEA is selected, GSM.CellSelection.NECI should be set to 1.  "
			"See GSM 04.08 Sections 9.1.8 and 10.5.2.4 for an explanation of the NECI bit.  "
			"Note that some handset models exhibit bugs when VEA is used and these bugs may affect performance."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("Control.WatchdogMinutes","0",
		"minutes",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::VALRANGE,
		"0:1440",
		false,
		"Number of minutes before the radio watchdog expires and OpenBTS is restarted, set to 0 to disable."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("GGSN.DNS","",
		"",
		ConfigurationKey::CUSTOMERWARN,
		ConfigurationKey::MIPADDRESS_OPT,// audited
		"",
		true,
		"The list of DNS servers to be used by downstream clients.  "
			"By default, DNS servers of the host system are used.  "
			"To override, specify a space-separated list of DNS servers, in IP dotted notation, eg: 1.2.3.4 5.6.7.8.  "
			"To use the host system DNS servers again, execute \"unconfig GGSN.DNS\"."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("GGSN.Firewall.Enable","1",
		"",
		ConfigurationKey::CUSTOMERWARN,
		ConfigurationKey::CHOICE,
		"0|Disable Firewall,"
			"1|Block MS Access to OpenBTS and Other MS,"
			"2|Block All Private IP Addresses",
		true,
		"0=no firewall; 1=block MS attempted access to OpenBTS or other MS; 2=block all private IP addresses."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("GGSN.IP.MaxPacketSize","1520",
		"bytes",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::VALRANGE,
		"1492:9000",// educated guess
		true,
		"Maximum size of an IP packet.  "
			"Should normally be 1520."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("GGSN.IP.ReuseTimeout","180",
		"seconds",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::VALRANGE,
		"120:240",// educated guess,
		true,
		"How long IP addresses are reserved after a session ends."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("GGSN.IP.TossDuplicatePackets","0",
		"",
		ConfigurationKey::CUSTOMER,
		ConfigurationKey::BOOLEAN,
		"",
		true,
		"Toss duplicate TCP/IP packets to prevent unnecessary traffic on the radio."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("GGSN.Logfile.Name","",
		"",
		ConfigurationKey::FACTORY,
		ConfigurationKey::FILEPATH_OPT,
		"",
		true,
		"If specified, internet traffic is logged to this file. E.g. ggsn.log."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("GGSN.MS.IP.Base","192.168.99.1",
		"",
		ConfigurationKey::CUSTOMERTUNE,
		ConfigurationKey::IPADDRESS,
		"",
		true,
		"Base IP address assigned to MS."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("GGSN.MS.IP.MaxCount","254",
		"addresses",
		ConfigurationKey::CUSTOMERTUNE,
		ConfigurationKey::VALRANGE,
		"1:254",// educated guess
		true,
		"Number of IP addresses to use for MS."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("GGSN.MS.IP.Route","",
		"",
		ConfigurationKey::CUSTOMERTUNE,
		ConfigurationKey::CIDR_OPT,// audited
		"",
		true,
		"A route address to be used for downstream clients.  "
			"By default, OpenBTS manufactures this value from the GGSN.MS.IP.Base assuming a 24 bit mask.  "
			"To override, specify a route address in the form xxx.xxx.xxx.xxx/yy.  "
			"The address must encompass all MS IP addresses.  "
			"To use the auto-generated value again, execute \"unconfig GGSN.MS.IP.Route\"."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("GGSN.ShellScript","",
		"",
		ConfigurationKey::CUSTOMERTUNE,
		ConfigurationKey::FILEPATH_OPT,// audited
		"",
		false,
		"A shell script to be invoked when MS devices attach or create IP connections.  "
			"By default, this feature is disabled.  "
			"To enable, specify an absolute path to the script you wish to execute e.g. /usr/bin/ms-attach.sh.  "
			"To disable again, execute \"unconfig GGSN.ShellScript\"."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("GGSN.TunName","sgsntun",
		"",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::STRING,
		"^[a-z0-9]+$",
		true,
		"Tunnel device name for GGSN."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("GPRS.advanceblocks","10",
		"blocks",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::VALRANGE,
		"5:15",// educated guess
		false,
		"Number of advance blocks to use in the CCCH reservation."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("GPRS.CellOptions.T3168Code","5",
		"",
		ConfigurationKey::CUSTOMERTUNE,
		ConfigurationKey::CHOICE,
		"0|500ms,"
			"1|1000ms,"
			"2|1500ms,"
			"3|2000ms,"
			"4|2500ms,"
			"5|3000ms,"
			"6|3500ms,"
			"7|4000ms",
			true,
			"Timer 3168 in the MS controls the wait time after sending a Packet Resource Request to initiate a TBF before giving up or reattempting a Packet Access Procedure, which may imply sending a new RACH.  "
				"This code is broadcast to the MS in the C0T0 beacon in the GPRS Cell Options IE.  "
				"See GSM 04.60 12.24.  "
				"Range 0..7, representing values from 0.5sec to 4sec in 0.5sec steps."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("GPRS.CellOptions.T3192Code","0",
		"",
		ConfigurationKey::CUSTOMERTUNE,
		ConfigurationKey::CHOICE,
		"0|500ms,"
			"1|1000ms,"
			"2|1500ms,"
			"3|0ms,"
			"4|80ms,"
			"5|120ms,"
			"6|160ms,"
			"7|200ms",
		true,
		"Timer 3192 in the MS specifies the time MS continues to listen on PDCH after all downlink TBFs are finished, and is used to reduce unnecessary RACH traffic.  "
			"This code is broadcast to the MS in the C0T0 beacon in the GPRS Cell Options IE. "
			"The value must be one of the codes described in GSM 04.60 12.24.  "
			"Value 0 implies 500msec; 2 implies 1500msec; 3 imples 0msec."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("GPRS.ChannelCodingControl.RSSI","-40",
		"dB",
		ConfigurationKey::CUSTOMERTUNE,
		ConfigurationKey::VALRANGE,
		"-65:-15",// educated guess
		false,
		"If the initial unlink signal strength is less than this amount in dB, GPRS uses a lower bandwidth but more robust encoding CS-1.  "
			"This value should normally be GSM.Radio.RSSITarget + 10 dB."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("GPRS.Channels.Congestion.Threshold","200",
		"probability in %",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::VALRANGE,
		"100:300(5)",// educated guess
		false,
		"The GPRS channel is considered congested if the desired bandwidth exceeds available bandwidth by this amount, specified in percent."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("GPRS.Channels.Congestion.Timer","60",
		"seconds",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::VALRANGE,
		"30:90(5)",// educated guess
		false,
		"How long in seconds GPRS congestion exceeds the Congestion.Threshold before we attempt to allocate another channel for GPRS."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("GPRS.Channels.Min.C0","2",
		"channels",
		ConfigurationKey::CUSTOMERTUNE,
		ConfigurationKey::VALRANGE,
		"0:7",
		true,
		"Minimum number of channels allocated for GPRS service on ARFCN C0."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("GPRS.Channels.Min.CN","0",
		"channels",
		ConfigurationKey::CUSTOMERTUNE,
		ConfigurationKey::VALRANGE,
		"0:100",
		true,
		"Minimum number of channels allocated for GPRS service on ARFCNs other than C0."
	);
	map[tmp.getName()] = tmp;
	}

#if GPRS_CHANNELS_MAX_SUPPORTED
	{ ConfigurationKey tmp("GPRS.Channels.Max","4",
		"channels",
		ConfigurationKey::CUSTOMERTUNE,
		ConfigurationKey::VALRANGE,
		"0:10",// educated guess
		false,
		"Maximum number of channels allocated for GPRS service."
	);
	map[tmp.getName()] = tmp;
	}
#endif

	// (pat 10-2013) Added commas in this list to make it more clear that the value is a list.
	// It does not matter whether commas appear in the string or not,
	// only appearance or non-appearance of the digits '1' .. '4' is significant.
	// You could even stick in "CS1,CS4" if the regular expression allowed it.
	{ ConfigurationKey tmp("GPRS.Codecs.Downlink","1,4",
		"",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::STRING_OPT,
		//"^1{0,1}2{0,1}3{0,1}4{0,1}$",// "1234" with each number optional
		"^[CS1234,]*$",	// "1,2,3,4" with each number optional, or CS1,CS2,CS3,CS4.
		false,
		"An empty value specifies GPRS may use all available codecs.  "
		"Otherwise list of allowed GPRS downlink codecs 1..4 for CS-1..CS-4.  Currently, only 1 and 4 are supported e.g. 1,4."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("GPRS.Codecs.Uplink","1,4",
		"",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::STRING_OPT,
		"^[CS1234,]*$",
		false,
		"An empty value specifies GPRS may use all available codecs.  "
		"Otherwise list of allowed GPRS uplink codecs 1..4 for CS-1..CS-4.  Currently, only 1 and 4 are supported e.g. 1,4."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("GPRS.Counters.Assign","10",
		"messages",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::VALRANGE,
		"5:15",// educated guess
		false,
		"Maximum number of assign messages sent."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("GPRS.Counters.N3101","20",
		"responses",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::VALRANGE,
		"8:32",// educated guess
		false,
		"Counts unused USF responses to detect nonresponsive MS.  "
			"Should be > 8.  "
			"See GSM04.60 Sec 13."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("GPRS.Counters.N3103","8",
		"attempts",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::VALRANGE,
		"4:12",// educated guess
		false,
		"Counts ACK/NACK attempts to detect nonresponsive MS.  "
			"See GSM04.60 sec 13."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("GPRS.Counters.N3105","12",
		"responses",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::VALRANGE,
		"6:18",// educated guess
		false,
		"Counts unused RRBP responses to detect nonresponsive MS.  "
			"See GSM04.60 Sec 13."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("GPRS.Counters.Reassign","6",
		"messages",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::VALRANGE,
		"3:9",// educated guess
		false,
		"Maximum number of reassign messages sent."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("GPRS.Counters.TbfRelease","5",
		"messages",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::VALRANGE,
		"3:8",// educated guess
		false,
		"Maximum number of TBF release messages sent."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("GPRS.Debug","0",
		"",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::BOOLEAN,
		"",
		false,
		"Toggle GPRS debugging."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("GPRS.Downlink.KeepAlive","300",
		"milliseconds",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::VALRANGE,
		"200:5000(100)",// educated guess
		false,
		"How often to send keep-alive messages for persistent TBFs in milliseconds; must be long enough to avoid simultaneous in-flight duplicates, and short enough that MS gets one every 5 seconds.  "
			"GSM 5.08 10.2.2 indicates MS must get a block every 360ms"
			// (oley) our allowed value range does not permit the recommended value of 360ms. Is this intentional?
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("GPRS.Downlink.Persist","0",
		"milliseconds",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::VALRANGE,
		"0:10000(100)",// educated guess
		false,
		"After completion, downlink TBFs are held open for this time in milliseconds.  "
			"If non-zero, must be greater than GPRS.Downlink.KeepAlive."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("GPRS.Enable","0",
		"",
		ConfigurationKey::CUSTOMER,
		ConfigurationKey::BOOLEAN,
		"",
		true,
		"If enabled, GPRS service is advertised in the C0T0 beacon, and GPRS service may be started on demand.  "
			"See also GPRS.Channels.*."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("GPRS.LocalTLLI.Enable","1",
		"",
		ConfigurationKey::CUSTOMERTUNE,
		ConfigurationKey::BOOLEAN,
		"",
		false,
		"Enable recognition of local TLLI."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("GPRS.MS.KeepExpiredCount","20",
		"structs",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::VALRANGE,
		"10:30",// eduated guess
		false,
		"How many expired MS structs to retain; they can be viewed with gprs list ms -x"
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("GPRS.MS.Power.Alpha","10",
		"alpha",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::CHOICE,
		"0|0.0,"
			"1|0.1,"
			"2|0.2,"
			"3|0.3,"
			"4|0.4,"
			"5|0.5,"
			"6|0.6,"
			"7|0.7,"
			"8|0.8,"
			"9|0.9,"
			"10|1.0",
		false,
		"MS power control parameter, unitless, in steps of 0.1, so a parameter of 5 is an alpha value of 0.5.  "
			"Determines sensitivity of handset to variations in downlink RXLEV.  "
			"Valid range is 0...10 for alpha values of 0...1.0.  "
			"See GSM 05.08 10.2.1."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("GPRS.MS.Power.Gamma","31",
		"2 dB steps",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::VALRANGE,
		"0:31",
		false,
		"MS power control parameter, in 2 dB steps.  "
			"Determines baseline of handset uplink power relative to downlink RXLEV.  "
			"The optimum value will tend to be lower for BTS units with higher power output.  "
			"This default assumes a balanced link with a BTS output of 2-4 W/ARFCN.  "
			"Valid range is 0...31 for gamma values of 0...62 dB.  "
			"See GSM 05.08 10.2.1."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("GPRS.MS.Power.T_AVG_T","15",
		"",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::VALRANGE,
		"0:25",
		true,
		"MS power control parameter; see GSM 05.08 10.2.1."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("GPRS.MS.Power.T_AVG_W","15",
		"",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::VALRANGE,
		"0:25",
		true,
		"MS power control parameter; see GSM 05.08 10.2.1."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("GPRS.Multislot.Max.Downlink","3",
		"channels",
		ConfigurationKey::CUSTOMERTUNE,
		ConfigurationKey::VALRANGE,
		"0:10",// educated guess
		false,
		"Maximum number of channels used for a single MS in downlink."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("GPRS.Multislot.Max.Uplink","2",
		"channels",
		ConfigurationKey::CUSTOMERTUNE,
		ConfigurationKey::VALRANGE,
		"0:10",// educated guess
		false,
		"Maximum number of channels used for a single MS in uplink."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("GPRS.NC.NetworkControlOrder","2",
		"",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::VALRANGE,
		"0:3",
		true,
		"Controls measurement reports and cell reselection mode (MS autonomous or under network control); should not be changed.  "
			"See GSM 5.08 10.1.4."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("GPRS.NMO","2",
		"",
		ConfigurationKey::CUSTOMERTUNE,
		ConfigurationKey::CHOICE,
		"1|Mode I,"
			"2|Mode II,"
			"3|Mode III",
		false,
		"Network Mode of Operation.  "
			"See GSM 03.60 Section 6.3.3.1 and 24.008 4.7.1.6.  "
			"Allowed values are 1, 2, 3 for modes I, II, III.  "
			"Mode II (2) is recommended.  "
			"Mode I implies combined routing updating procedures."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("GPRS.PRIORITY-ACCESS-THR","6",
		"",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::CHOICE,
		"0|Packet access not allowed in the cell,"
			"3|Packet access allowed for priority level 1,"
			"4|Packet access allowed for priority level 1 to 2,"
			"5|Packet access allowed for priority level 1 to 3,"
			"6|Packet access allowed for priority level 1 to 4",
		true,
		"Code contols GPRS packet access priorities allowed.  "
			"See GSM04.08 table  10.5.76."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("GPRS.RAC","0",
		"",
		ConfigurationKey::CUSTOMERTUNE,
		ConfigurationKey::VALRANGE,
		"0:255",
		true,
		"GPRS Routing Area Code, advertised in the C0T0 beacon."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("GPRS.RA_COLOUR","0",
		"",
		ConfigurationKey::CUSTOMERTUNE,
		ConfigurationKey::VALRANGE,
		"0:7",
		false,
		"GPRS Routing Area Color as advertised in the C0T0 beacon."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("GPRS.RRBP.Min","0",
		"reservations",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::VALRANGE,
		"0:3",
		false,
		"Minimum value for Relative Reserved Block Period (RRBP) reservations, range 0..3.  "
			"Should normally be 0.  "
			"A non-zero value gives the MS more time to respond to the RRBP request."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("GPRS.Reassign.Enable","1",
		"",
		ConfigurationKey::CUSTOMERTUNE,
		ConfigurationKey::BOOLEAN,
		"",
		false,
		"Enable TBF Reassignment."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("GPRS.SendIdleFrames","0",
		"",
		ConfigurationKey::FACTORY,
		ConfigurationKey::BOOLEAN,
		"",
		false,
		"Should be 0 for current transceiver or 1 for deprecated version of transceiver."
	);
	map[tmp.getName()] = tmp;
	}

#if 0	// (pat) obsolete
	{ ConfigurationKey tmp("GPRS.SGSN.port","1920",
		"",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::PORT,
		"",
		false,
		"Port number of the SGSN required for GPRS service.  This must match the port specified in the SGSN config file, currently osmo_sgsn.cfg."
	);
	map[tmp.getName()] = tmp;
	}
#endif

	{ ConfigurationKey tmp("GPRS.TBF.Downlink.Poll1","10",
		"blocks",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::VALRANGE,
		"5:15",// educated guess
		false,
		"When the first poll is sent for a downlink tbf, measured in blocks sent."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("GPRS.TBF.EST","1",
		"",
		ConfigurationKey::CUSTOMERTUNE,
		ConfigurationKey::BOOLEAN,
		"",
		false,
		"Allow MS to request another uplink assignment at end up of uplink TBF.  "
			"See GSM 4.60 9.2.3.4."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("GPRS.TBF.Expire","30000",
		"milliseconds",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::VALRANGE,
		"20000:40000",// educated guess
		false,
		"How long in milliseconds to try before giving up on a TBF."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("GPRS.TBF.KeepExpiredCount","20",
		"structs",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::VALRANGE,
		"15:25",// educated guess
		false,
		"How many expired TBF structs to retain; they can be viewed with gprs list tbf -x."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("GPRS.TBF.Retry","1",
		"",
		ConfigurationKey::CUSTOMERTUNE,
		ConfigurationKey::CHOICE,
		"0|Do Not Retry,"
			"1|Codec 1,"
			"2|Codec 2,"
			"3|Codec 3,"
			"4|Codec 4",
		false,
		"If 0, no tbf retry, otherwise if a tbf fails it will be retried with this codec, numbered 1..4."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("GPRS.Timers.Channels.Idle","6000",
		"milliseconds",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::VALRANGE,
		"3000:6000(100)",// educated guess
		false,
		"How long in milliseconds a GPRS channel is idle before being returned to the pool of channels.  "
			"Also depends on Channels.Min.  "
			"Currently the channel cannot be returned to the pool while there is any GPRS activity on any channel."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("GPRS.Timers.MS.Idle","600",
		"seconds",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::VALRANGE,
		"300:900(10)",// educated guess
		false,
		"How long in seconds an MS is idle before the BTS forgets about it."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("GPRS.Timers.MS.NonResponsive","6000",
		"milliseconds",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::VALRANGE,
		"3000:9000(100)",// educated guess
		false,
		"How long in milliseconds a TBF is non-responsive before the BTS kills it."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("GPRS.Timers.T3169","5000",
		"milliseconds",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::VALRANGE,
		"2500:7500(100)",// educated guess
		false,
		"Nonresponsive uplink TBF resource release timer, in milliseconds.  "
			"See GSM04.60 Sec 13."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("GPRS.Timers.T3191","5000",
		"milliseconds",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::VALRANGE,
		"2500:7500(100)",// educated guess
		false,
		"Nonresponsive downlink TBF resource release timer, in milliseconds.  "
			"See GSM04.60 Sec 13."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("GPRS.Timers.T3193","0",
		"milliseconds",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::VALRANGE,
		"0:5000(100)",// educated guess
		false,
		"Timer T3193 (in milliseconds) in the base station corresponds to T3192 in the MS, which is set by GPRS.CellOptions.T3192Code.  "
			"The T3193 value should be slightly longer than that specified by the T3192Code.  "
			"If 0, the BTS will fill in a default value based on T3192Code."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("GPRS.Timers.T3195","5000",
		"milliseconds",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::VALRANGE,
		"2500:7500(100)",// educated guess
		false,
		"Nonresponsive downlink TBF resource release timer, in milliseconds.  "
			"See GSM04.60 Sec 13."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("GPRS.Uplink.KeepAlive","300",
		"milliseconds",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::VALRANGE,
		"200:5000(100)",// educated guess
		false,
		"How often to send keep-alive messages for persistent TBFs in milliseconds; must be long enough to avoid simultaneous in-flight duplicates, and short enough that MS gets one every 5 seconds."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("GPRS.Uplink.Persist","4000",
		"milliseconds",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::VALRANGE,
		"0:6000(100)",// educated guess
		true,
		"After completion uplink TBFs are held open for this time in milliseconds.  "
			"If non-zero, must be greater than GPRS.Uplink.KeepAlive.  "
			"This is broadcast in the beacon and cannot be changed once BTS is started."
	);
	map[tmp.getName()] = tmp;
	}

	// (pat 6-2014) Changed default to "auto", and we'll just fish for the phone number in all the possible places. See code in SIPDialog.cpp
	{ ConfigurationKey tmp("GSM.CallerID.Source","auto",			//"username",
		"",
		ConfigurationKey::CUSTOMERTUNE,
		ConfigurationKey::CHOICE,
		"auto,displayname,username,p-asserted-identity",
		false,
		"The source for numeric Caller ID has traditionally been the username field. After version 4.0 this behavior "
			"will be changed to use the displayname field as it is a more accepted practice. This parameter will "
			"allow those with existing integrations to easily return to the legacy behavior until their SIP "
			"switches can be reconfigured. Additionally, using the P-Asserted-Identity header to source the "
			"Caller ID number is supported."
	);
	map[tmp.getName()] = tmp;
	}

	// (pat 2-2014) Beacon overall configuration.  We now support at least type 0 and 1.
	// This value is broadcast on the beacon and may not be changed after startup.
	{ ConfigurationKey tmp("GSM.CCCH.CCCH-CONF","1",
		"",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::CHOICE,
		"0|single Combination-IV Beacon (with 9 CCCH) on timeslot 0,"
		"1|single Combination-V Beacon (with 3 CCCH + 4 SDCCH),"
		"2|2 x Combination-IV Beacons on timeslots 0+2,"
		"4|3 x Combination-IV Beacons on timeslots 0+2+4,"
		"6|4 x Combination-IV Beacons on timeslots 0+2+4+6",
		true,
		"CCCH configuration type.  Defined in GSM 5.02 3.3.2.3.  "
			"DO NOT CHANGE THIS.  Value is fixed by the implementation.  "
	);
	map[tmp.getName()] = tmp;
	}

	// (pat 2-2014) Magic paging control parameter.
	{ ConfigurationKey tmp("GSM.CCCH.BS_AG_BLKS_RES","auto",
		"",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::CHOICE,
		"auto,0,1,2,3,4,5,6,7",
		true,
		"CCCH paging configuration: number of CCCH blocks reserved for AGCH.  Defined in GSM 5.02 3.3.2.3.  "
		"Value auto means OpenBTS picks a reasonable value, "
		"otherwise range is 0..2 for a Combination-V beacon or 0..7 for a Combination-IV beacon.  "
		"Only super-experts should set this to any value other than auto.  "
	);
	map[tmp.getName()] = tmp;
	}

	// (pat 2-2014) Magic paging control parameter.
	{ ConfigurationKey tmp("GSM.CCCH.BS_PA_MFRMS","2",
		"",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::CHOICE,
		"2,3,4,5,6,7,8,9",
		true,
		"CCCH paging configuration: number of Paging Multiframes.  Defined in GSM 5.02 3.3.2.3.  "
		"Only super-experts should change this.  "
	);
	map[tmp.getName()] = tmp;
	}

#if unused	// (pat 4-24-2014) no longer implemented
	// (pat) This option is here so it can be disabled to help testing Immediate Assignment Reject behavior;
	// if this is non-zero you cant test it in the lab because the phone is always close enough to not be rejected.
	{ ConfigurationKey tmp("GSM.CCCH.FavorTA","0",
		"",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::VALRANGE,
		"0:62",
		true,
		"When there is congestion, handsets closer to the cell tower than this many timing advance units are processed first.  "
		"Value 0 disables, ie, all handsets treated equally.  "
	);
	map[tmp.getName()] = tmp;
	}
#endif

	{ ConfigurationKey tmp("GSM.CellOptions.RADIO-LINK-TIMEOUT","15",	// 15 is roughly 7.5 seconds.
		"480ms periods",
		ConfigurationKey::CUSTOMERTUNE,
		ConfigurationKey::VALRANGE,
		"10:120",// educated guess
		true,
		"Number of failed SACCH reports before an MS declares a physical link dead.  GSM 5.08 5.2.  SACCH reports are at 480ms intervals.  "
		"This value, converted to seconds, should be less than GSM.Timer.T3109. "
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("GSM.BTS.RADIO_LINK_TIMEOUT","15",	// 15 is roughly 7.5 seconds.
		"480ms periods",
		ConfigurationKey::CUSTOMERTUNE,
		ConfigurationKey::VALRANGE,
		"10:120",// educated guess
		true,
		"Number of failed SACCH reports before the BTS declares a physical link dead.  GSM 5.08 5.2 and 5.3.  SACCH reports are at 480ms intervals.  "
		"Similar to GSM.CellOptions.RADIO-LINK-TIMEOUT which serves the same purpose in the MS.  "
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("GSM.CellSelection.CELL-RESELECT-HYSTERESIS","3",
		"",
		ConfigurationKey::CUSTOMERTUNE,
		ConfigurationKey::CHOICE,
		"0|0dB,"
			"1|2dB,"
			"2|4dB,"
			"3|6dB,"
			"4|8dB,"
			"5|10dB,"
			"6|12dB,"
			"7|14dB",
		false,
		"Cell Reselection Hysteresis.  "
			"See GSM 04.08 10.5.2.4, Table 10.5.23 for encoding.  "
			"Encoding is $2N$ dB, values of $N$ are 0...7 for 0...14 dB."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("GSM.CellSelection.MS-TXPWR-MAX-CCH","0",
		"dB",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::VALRANGE,
		"0:31",
		false,
		"Cell selection parameters.  "
			"See GSM 04.08 10.5.2.4."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("GSM.CellSelection.NCCsPermitted","0",
		"",
		ConfigurationKey::CUSTOMERWARN,
		ConfigurationKey::VALRANGE,
		"0:255",
		false,
		"NCCs Permitted.  "
			"An 8-bit mask of allowed NCCs.  "
			"The NCC of your own network is automatically included.  "
			"Unless you are coordinating with another carrier, this should be left at zero."
	);
	map[tmp.getName()] = tmp;
	}

	// (pat) In GSM 4.08 10.5.2.4: NECI is labeled "half rate support"?
	{ ConfigurationKey tmp("GSM.CellSelection.NECI","1",
		"",
		ConfigurationKey::CUSTOMERWARN,
		ConfigurationKey::CHOICE,
		"0|New establishment causes are not supported,"
			"1|New establishment causes are supported",
		false,
		"NECI, New Establishment Causes.  "
			"This must be set to 1 if you want to support very early assignment (VEA).  "
			"It can be set to 1 even if you do not use VEA, so you might as well leave it as 1.  "
			"See GSM 04.08 10.5.2.4, Table 10.5.23 and 04.08 9.1.8, Table 9.9 and the Control.VEA parameter."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("GSM.CellSelection.RXLEV-ACCESS-MIN","0",
		"dB",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::VALRANGE,
		"0:63",
		false,
		"Cell selection parameters.  "
			"See GSM 04.08 10.5.2.4."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("GSM.Channels.C1sFirst","0",
		"",
		ConfigurationKey::CUSTOMERTUNE,
		ConfigurationKey::BOOLEAN,
		"",
		true,
		"Allocate C-I slots first, starting at C0T1.  "
			"Otherwise, allocate C-VII slots first."
	);
	map[tmp.getName()] = tmp;
	}

	// (pat) 3-2014 added 'auto' and made it the default.
	{ ConfigurationKey tmp("GSM.Channels.NumC1s","auto",
		"timeslots",
		ConfigurationKey::CUSTOMERTUNE,
		ConfigurationKey::REGEX,
		"^auto$|^[0-9]+$",		// It is calculated or validated on startup.
		true,
		"Number of Combination-I timeslots to configure.  "
			"The Combination-1 timeslot carries a single full-rate TCH, used for speech calling.  "
			"If value is auto, OpenBTS picks the value by using all otherwise unused timeslots.  "
	);
	map[tmp.getName()] = tmp;
	}

	// (pat) 3-2014 added 'auto' and made it the default.
	{ ConfigurationKey tmp("GSM.Channels.NumC7s","auto",
		"timeslots",
		ConfigurationKey::CUSTOMERTUNE,
		ConfigurationKey::REGEX,
		"^auto$|^[0-9]+$",		// It is calculated or validated on startup.
		true,
		"Number of Combination-VII timeslots to configure.  "
			"The Combination-7 timeslot carries 8 SDCCHs, useful to handle high registration loads or SMS.  "
			"If value is auto, OpenBTS picks the value based on beacon type and number of ARFCNs.  "
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("GSM.Channels.SDCCHReserve","0",
		"SDCCHs",
		ConfigurationKey::CUSTOMERTUNE,
		ConfigurationKey::VALRANGE,
		"0:10",// educated guess
		false,
		"Number of SDCCHs to reserve for non-LUR operations.  "
			"This can be used to force LUR transactions into a lower priority."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("GSM.Cipher.CCHBER","0",
		"",
		ConfigurationKey::CUSTOMERWARN,
		ConfigurationKey::VALRANGE,
		"0.0:1.0",
		false,
		"Probability of a bit getting toggled in a control channel burst for cracking protection."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("GSM.Cipher.Encrypt","0",
		"",
		ConfigurationKey::CUSTOMERWARN,
		ConfigurationKey::BOOLEAN,
		"",
		false,
		"Encrypt traffic between MS and OpenBTS."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("GSM.Cipher.RandomNeighbor","0",
		"",
		ConfigurationKey::CUSTOMERWARN,
		ConfigurationKey::VALRANGE,
		"0.0:1.0",
		false,
		"Probability of a random neighbor being added to SI5 for cracking protection."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("GSM.Cipher.ScrambleFiller","0",
		"",
		ConfigurationKey::CUSTOMERWARN,
		ConfigurationKey::BOOLEAN,
		"",
		false,
		"Scramble filler in layer 2 for cracking protection."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("GSM.Control.GPRSMaxIgnore","5",
		"messages",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::VALRANGE,
		"3:8",// educated guess
		false,
		"Ignore GPRS messages on GSM control channels.  "
			"Value is number of consecutive messages to ignore."
	);
	map[tmp.getName()] = tmp;
	}


	{ ConfigurationKey tmp("GSM.Timer.Handover.Holdoff","10",
		"seconds",
		ConfigurationKey::CUSTOMERTUNE,
		ConfigurationKey::VALRANGE,
		"1:120",
		true,
		"Handover will not be permitted until this time has elapsed after an initial channel seizure or handover."
	);
	map[tmp.getName()] = tmp;
	}



	{ ConfigurationKey tmp("GSM.Identity.BSIC.BCC","2",
		"",
		ConfigurationKey::CUSTOMERSITE,
		ConfigurationKey::VALRANGE,
		"0:7",
		false,
		"GSM basestation color code; lower 3 bits of the BSIC.  "
			"BCC values in a multi-BTS network should be assigned so that BTS units with overlapping coverage do not share a BCC.  "
			"This value will also select the training sequence used for all slots on this unit.",
		ConfigurationKey::NEIGHBORSUNIQUE
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("GSM.Identity.BSIC.NCC","0",
		"",
		ConfigurationKey::CUSTOMERSITE,
		ConfigurationKey::VALRANGE,
		"0:7",
		false,
		"GSM network color code; upper 3 bits of the BSIC.  "
			"Assigned by your national regulator.  "
			"Must be distinct from NCCs of other GSM operators in your area.",
		ConfigurationKey::GLOBALLYSAME
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("GSM.Identity.CI","10",
		"",
		ConfigurationKey::CUSTOMERSITE,
		ConfigurationKey::VALRANGE,
		"0:65535",
		false,
		"Cell ID, 16 bits.  "
			"In some cases, the last digit of the cell id represents the sector id. "
			"A last digit of 0 is used for an omnidirectional antenna.  "	
			"A last digit of 1, 2, 3, etc indicates a sector of the multi-sector antenna.  "	
			"Should be unique.",
		ConfigurationKey::GLOBALLYUNIQUE
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("GSM.Identity.LAC","1000",
		"",
		ConfigurationKey::CUSTOMERSITE,
		ConfigurationKey::VALRANGE,
		"0:65280",
		false,
		"Location area code, 16 bits, values 0xFFxx are reserved.  "
			"For multi-BTS networks, assign a unique LAC to each BTS unit.  "
			"(This is not the normal procedure in conventional GSM networks, but is the correct procedure in OpenBTS networks.)",
		ConfigurationKey::GLOBALLYUNIQUE
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("GSM.Identity.MCC","001",
		"",
		ConfigurationKey::CUSTOMERSITE,
		ConfigurationKey::STRING,
		"^[0-9]{3}$",
		false,
		"Mobile country code; must be three digits.  "
			"Defined in ITU-T E.212. Value of 001 for test networks.",
		ConfigurationKey::GLOBALLYSAME
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("GSM.Identity.MNC","01",
		"",
		ConfigurationKey::CUSTOMERSITE,
		ConfigurationKey::STRING,
		"^[0-9]{2,3}$",
		false,
		"Mobile network code, two or three digits.  "
			"Assigned by your national regulator.  "
			"01 for test networks.",
		ConfigurationKey::GLOBALLYSAME
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("GSM.Identity.ShortName","Range",
		"",
		ConfigurationKey::CUSTOMERSITE,
		ConfigurationKey::STRING,
		"^[0-9a-zA-Z]+$",
		false,
		"Network short name, displayed on some phones.  "
			"Optional but must be defined if you also want the network to send time-of-day.",
		ConfigurationKey::GLOBALLYSAME
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("GSM.MS.Power.Damping","75",
		"damping value in percent",
		ConfigurationKey::CUSTOMERTUNE,
		ConfigurationKey::VALRANGE,
		"0:100",
		false,
		"Damping value for MS power control loop in percent.  The ordered MS power is based on RSSI [Received Signal Strength Indication].  "
		"A value of 100 here ignores RSSI and SNR entirely;  "
		"a value of 0 causes the MS power to change instantaneously based on RSSI or SNR, which is inadvisable because it sets up power oscillations.  "
		"The ordered MS power is then clamped between GSM.MS.Power.Max and GSM.MS.Power.Min.  "
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("GSM.MS.Power.Max","33",
		"dBm",
		ConfigurationKey::CUSTOMERTUNE,
		ConfigurationKey::VALRANGE,
		"0:39",
		false,
		"Maximum commanded MS power level in dBm."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("GSM.MS.Power.Min","5",
		"dBm",
		ConfigurationKey::CUSTOMERTUNE,
		ConfigurationKey::VALRANGE,
		"0:39",
		false,
		"Minimum commanded MS power level in dBm."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("GSM.MS.TA.Damping","50",
		"damping value in percent",
		ConfigurationKey::CUSTOMERTUNE,
		ConfigurationKey::VALRANGE,
		"5:90",// educated guess
		false,
		"Damping value for TA [timing advance] control loop, which specifies the TA to be applied by the MS.  "
		"This damping factor is meant to prevent a single bad incoming TA estimate from moving the TA value out of range.  "
		// (pat) But does it work?
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("GSM.MS.TA.Max","62",
		"symbol periods",
		ConfigurationKey::CUSTOMERTUNE,
		ConfigurationKey::VALRANGE,
		"1:62",
		false,
		"Maximum allowed timing advance in symbol periods.  "
			"One symbol period of round-trip delay is about 0.55 km of distance.  "
			"Ignore RACH bursts with delays greater than this.  "
			"Can be used to limit service range.  "
			"Valid range is 1..62."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("GSM.MaxSpeechLatency","2",
		"20 millisecond frames",
		ConfigurationKey::CUSTOMERTUNE,
		ConfigurationKey::VALRANGE,
		"1:5",// educated guess
		false,
		"Maximum allowed speech buffering latency, in 20 millisecond frames.  "
			"If the jitter is larger than this delay, frames will be lost."
	);
	map[tmp.getName()] = tmp;
	}

	// (pat 8-2013) Added this to provide control over the RTP buffers.
	// Originally the adaptive algorithm in the RTP library did not work very well, so you can over-ride it here.
	// I turned off the RTP scheduler, and add the rtp timestamp-jump-callback, which vastly improved
	// the audio quality so we probably do not need to goof with this any more.  However, it is useful to completely
	// turn off the adaptive algorithm, which might work better than leaving it on.
	// WARNING:  After a discontinuity in the transmit data stream, the receive data stream fails to resynchronize
	// when this value is set much above 200 (I never determined the exact number), so I dont allow it.
	{ ConfigurationKey tmp("GSM.SpeechBuffer","1",
		"milliseconds",
		ConfigurationKey::CUSTOMERTUNE,
		ConfigurationKey::VALRANGE,
		"0:200",
		false,
		"Size of speech buffer in milliseconds.  If set to 0, no RTP speech buffer is used.  "
		"If set to 1, the RTP speech buffer size is determined adaptively.  "
		"Any other value sets the speech buffer size.  "
		"The speech buffer is needed to overcome jitter caused by natural variation in the internet traffic delay.  "
		"Note that speech is noticeably delayed by this amount, so we want to keep it as low as possible and still have reasonably reliable delivery.  "
		"The specified delay is in addition to the intrinsic buffering inside OpenBTS.  "
		"This value is used only at the start of a call; changing it does not affect on-going calls.  "
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("GSM.Neighbors","",
		"",
		ConfigurationKey::CUSTOMERWARN,
		ConfigurationKey::MIPADDRESS_OPT,// audited
		"",
		false,
		"A list of IP addresses of neighbor BTSs available for handover.  "
			"By default handover is disabled.  "
			"To enable, specify a space-separated list of a maximum of 31 OpenBTS IP addresses in IP dotted notation, "
			"optionally followed by a colon and the port number. E.g.: 1.2.3.4 5.6.7.8:16001.  "
			"To disable again, execute \"unconfig GSM.Neighbors\".",
		ConfigurationKey::NODESPECIFIC
	);
	map[tmp.getName()] = tmp;
	}


	// (pat) This seems redundant with GSM.Neighbors, because if you want to limit the number of neighbors sent
	// you can just leave them out of GSM.Neighbors.  But not quite - this is a limit on the number of neighbors
	// from the GSM.Neighbors list who actually respond to the Peer ping.
	// Why would you put a bad IP addresses in the GSM.Neighbors list?  I dont know.
	// If we really wanted to implement >32 neighbors, we would continually rotate all (>31) possible neighbors through the beacon to test
	// whether the phones in the cell can find them.
	{ ConfigurationKey tmp("GSM.Neighbors.NumToSend","31",	// (pat) Increased from 8 to 31.  Dont know why it was limited.
		"neighbors",
		ConfigurationKey::CUSTOMERTUNE,
		ConfigurationKey::VALRANGE,
		"0:31", 	// (pat) The measurement report (GSM 44.018 10.5.2.20) frequency index is 5 bits, but the value 31 has a special meaning for 3G so we avoid it, thus, max is 31 not 32.
					// The value 0 would effectively disable handover.
		false,
		"Maximum number of neighbors to send to handset in the neighbor list broadcast in the beacon."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("GSM.RACH.AC","0x0400",
		"",
		ConfigurationKey::CUSTOMERWARN,
		ConfigurationKey::CHOICE,
		"0|Full Access,"
			"0x0400|Emergency Calls Not Supported",
		false,
		"Access class flags.  "
			"This is the raw parameter sent on the BCCH.  "
			"See GSM 04.08 10.5.2.29 for encoding.  "
			"Set to 0 to allow full access.  "
			"Set to 0x0400 to indicate no support for emergency calls."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("GSM.RACH.MaxRetrans","1",
		"",
		ConfigurationKey::CUSTOMERTUNE,
		ConfigurationKey::CHOICE,
		"0|1 retransmission,"
			"1|2 retransmissions,"
			"2|4 retransmissions,"
			"3|7 retransmissions",
		false,
		"Maximum RACH retransmission attempts.  "
			"This is the raw parameter sent on the BCCH.  "
			"See GSM 04.08 10.5.2.29 for encoding."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("GSM.RACH.TxInteger","14",
		"",
		ConfigurationKey::CUSTOMERWARN,
		ConfigurationKey::CHOICE,
			"0|T=3 S=55 or 41 slots,"
			"1|T=4 S=76 or 52 slots,"
			"2|T=5 S=109 or 58 slots,"
			"3|T=6 S=163 or 86 slots,"
			"4|T=7 S=217 or 115 slots,"
			"5|T=8 S=55 or 41 slots,"
			"6|T=9 S=76 or 52 slots,"
			"7|T=10 S=109 or 58 slots,"
			"8|T=11 S=163 or 86 slots,"
			"9|T=12 S=217 or 115 slots,"
			"10|T=14 S=55 or 41 slots,"
			"11|T=16 S=76 or 52 slots,"
			"12|T=20 S=109 or 58 slots,"
			"13|T=25 S=163 or 86 slots,"
			"14|T=32 S=217 or 115 slots,"
			"15|T=50 S=55 or 41 slots",
		false,
		"Parameter to spread RACH busts over time.  "
			"Warning: changing this parameter may cause intermittent channel acquisition failures.  "
			"This is the raw parameter sent on the BCCH used to determine the S (delay) and T (spread) parameters.  "
			"In the description for S: the first value is for beacon CCCH-CONF type 0,2,4 or 6 (non-combined CCCH) and the "
			"second value is for beacon CCCH-CONF type 1 (combined CCCH+SDCCH.)  "
			"See GSM 04.08 10.5.2.29 and 3.3.1.1.2 for encoding; 05.02 clause 7 table 3 and 5 for definition of a RACH slot."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("GSM.RRLP.ACCURACY","40",
		"",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::VALRANGE,
		"20:60",// educated guess
		false,
		"Requested accuracy of location request. "
			"K in r=10(1.1**K-1), where r is the accuracy in meters. "
			"See 3GPP 03.32 Sec 6.2."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("GSM.RRLP.ALMANAC.ASSIST.PRESENT","0",
		"",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::BOOLEAN,
		"",
		false,
		"Send almanac info to mobile."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("GSM.RRLP.ALMANAC.REFRESH.TIME","24.0",
		"hours",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::VALRANGE,
		"18.0:30.0(0.1)",// educated guess
		false,
		"How often the almanac is refreshed, in hours."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("GSM.RRLP.ALMANAC.URL","http://www.navcen.uscg.gov/?pageName=currentAlmanac&format=yuma",
		"",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::STRING,
		"^(http|ftp)://[0-9a-zA-Z_.-]",
		false,
		"URL of the almanac source."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("GSM.RRLP.EPHEMERIS.ASSIST.COUNT","9",
		"satellites",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::VALRANGE,
		"6:12",// educated guess
		false,
		"Number of satellites to include in navigation model."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("GSM.RRLP.EPHEMERIS.REFRESH.TIME","1.0",
		"hours",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::VALRANGE,
		"0.5:1.5(0.1)",// educated guess
		false,
		"How often the ephemeris is refreshed, in hours."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("GSM.RRLP.EPHEMERIS.URL","ftp://ftp.trimble.com/pub/eph/CurRnxN.nav",
		"",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::STRING,
		"^(http|ftp)://[0-9a-zA-Z_.-]",
		false,
		"URL of ephemeris source."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("GSM.RRLP.RESPONSETIME","4",
		"",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::VALRANGE,
		"2:6",// educated guess
		false,
		"Mobile timeout.  "
			"(OpenBTS timeout is 130 sec = max response time + 2.) N in 2**N. "
			"See 3GPP 04.31 Sec A.2.2.1."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("GSM.RRLP.SEED.ALTITUDE","0",
		"meters",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::VALRANGE,
		"-420:8850(5)",
		false,
		"Seed altitude in meters wrt geoidal surface."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("GSM.RRLP.SEED.LATITUDE","37.777423",
		"degrees",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::VALRANGE,
		"-90.000000:90.000000",
		false,
		"Seed latitude in degrees: "
			"-90 (south pole) .. +90 (north pole)."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("GSM.RRLP.SEED.LONGITUDE","-122.39807",
		"degrees",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::VALRANGE,
		"-180.000000:180.000000",
		false,
		"Seed longitude in degrees: "
			"-180 (west of greenwich) .. +180 (east)."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("GSM.RRLP.SERVER.URL","",
		"",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::STRING_OPT,// audited
		"^(http|ftp)://[0-9a-zA-Z_.-]",
		false,
		"URL of RRLP server.  "
			"By default, this feature is disabled.  "
			"To enable, specify a server URL eg: http://localhost/cgi/rrlpserver.cgi.  "
			"To disable again, execute \"unconfig GSM.RRLP.SERVER.URL\"."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("GSM.Radio.ARFCNs","1",
		"ARFCNs",
		ConfigurationKey::CUSTOMERWARN,
		ConfigurationKey::VALRANGE,
		"1:10",// educated guess
		true,
		"The number of ARFCNs to use.  "
			"The ARFCN set will be C0, C0+2, C0+4, etc."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("GSM.Radio.Band","900",
		"",
		ConfigurationKey::CUSTOMERWARN,
		ConfigurationKey::CHOICE,
		"850|GSM850,"
			"900|PGSM900,"
			"1800|DCS1800,"
			"1900|PCS1900",
		true,
		"The GSM operating band.  "
			"Valid values are 850 for GSM850, 900 for PGSM900, 1800 for DCS1800 and 1900 for PCS1900.  "
			"For non-multiband units, this value is dictated by the hardware and should not be changed.",
		ConfigurationKey::GLOBALLYSAME
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("GSM.Radio.C0","51",
		"",
		ConfigurationKey::CUSTOMERSITE,
		ConfigurationKey::CHOICE,
		getARFCNsString(900),
		true,
		"The C0 ARFCN.  "
			"Also the base ARFCN for a multi-ARFCN configuration.",
		ConfigurationKey::NEIGHBORSUNIQUE
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("GSM.Radio.MaxExpectedDelaySpread","4",
		"symbol periods",
		ConfigurationKey::CUSTOMERTUNE,
		ConfigurationKey::VALRANGE,
		"1:4",
		false,
		"Expected worst-case delay spread in symbol periods, roughly 3.7 us or 1.1 km per unit.  "
			"This parameter is dependent on the terrain type in the installation area.  "
			"Typical values are: 1 for open terrain and small coverage areas, a value of 4 is strongly recommended for large coverage areas.  "
			"This parameter has a large effect on computational requirements of the software radio; values greater than 4 should be avoided."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("GSM.Radio.NeedBSIC","0",
		"",
		ConfigurationKey::FACTORY,
		ConfigurationKey::BOOLEAN,
		"",
		false,
		"Whether the Radio type requires the full BSIC."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("GSM.Radio.PowerManager.MaxAttenDB","10",
		"dB",
		ConfigurationKey::CUSTOMERTUNE,
		ConfigurationKey::VALRANGE,
		"0:80",// educated guess
		false,
		"Maximum transmitter attenuation level, in dB wrt full scale on the D/A output.  "
			"This sets the minimum power output level in the output power control loop."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("GSM.Radio.PowerManager.MinAttenDB","0",
		"dB",
		ConfigurationKey::CUSTOMERTUNE,
		ConfigurationKey::VALRANGE,
		"0:80",// educated guess
		false,
		"Minimum transmitter attenuation level, in dB wrt full scale on the D/A output.  "
			"This sets the maximum power output level in the output power control loop."
	);
	map[tmp.getName()] = tmp;
	}

	// (pat) 3-2014: Power Manager algorithm changed so OpenBTS starts at min power (MaxAttenDB) and slowly ramps up to max power (MinAttenDB).
	// This variable controls the ramp-up time.
	{ ConfigurationKey tmp("GSM.Radio.PowerManager.RampTime","60",
		"seconds",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::VALRANGE,
		"0:1000",
		false,
		"On start-up, OpenBTS ramps up power from GSM.Radio.PowerManager.MaxAttenDB to GSM.Radio.PowerManager.MinAttenDB.  "
		"This value is the duration of the ramp period in seconds.  "
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("GSM.Radio.RSSITarget","-50",
		"dB",
		ConfigurationKey::CUSTOMERTUNE,
		ConfigurationKey::VALRANGE,
		"-75:-25",// educated guess
		false,
		"Target uplink RSSI (Received Signal Strength Indication) for MS power control loop, in dB wrt to A/D full scale.  "
		"The MS power control loop adjusts MS TXPWR (transmit power) to try to keep RSSI at this level, "
		"or to satisfy GSM.Radio.SNRTarget, whichever requires more power.  "
			"Should be 6-10 dB above the noise floor."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("GSM.Radio.RSSIAveragePeriod","8",
		"samples",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::VALRANGE,
		"1:1000",
		false,
		"Number of RSSI samples averaged over when computing RSSI to compare to RSSITarget in the MS power control loop.  "
		"If this number is too low the ordered MS TXPWR may bounce around unnecessarily.  "
		"If this number is too high the MS TXPWR may not change quickly enough to respond to changing conditions.  "
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("GSM.Radio.SNRTarget","10",
		"ratio",
		ConfigurationKey::CUSTOMERTUNE,
		ConfigurationKey::VALRANGE,
		"6:20",// educated guess
		false,
		"The MS power control loop adjusts MS TXPWR (transmit power) to try to keep SNR (Signal to Noise Ratio) above this level."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("GSM.Radio.SNRAveragePeriod","8",
		"samples",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::VALRANGE,
		"1:1000",
		false,
		"Number of SNR samples averaged over when computing SNR to compare to SNRTarget in the MS power control loop.  "
		"If this number is too low the ordered MS TXPWR may bounce around unnecessarily.  "
		"If this number is too high the MS TXPWR may not change quickly enough to respond to changing conditions.  "
		"This is not the control variable used for handover decisions. "
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("GSM.Radio.RxGain","47",
		"dB",
		ConfigurationKey::FACTORY,
		ConfigurationKey::VALRANGE,
		"0:75",
		true,
		"Receiver gain setting in dB.  "
			"Ideal value is dictated by the hardware; 47 dB for RAD1 and 0-10 dB for Ettus hardware.  "
			"This database parameter is static but the receiver gain can be modified in real time with the CLI \"rxgain\" command.",
		ConfigurationKey::NODESPECIFIC
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("GSM.ShowCountry","0",
		"",
		ConfigurationKey::CUSTOMER,
		ConfigurationKey::BOOLEAN,
		"",
		false,
		"Tell the MS to show the country name based on the MCC."
	);
	map[tmp.getName()] = tmp;
	}

	// (pat) 5 seconds is not enough.  A handover failure on the Blackberry takes exactly 10 seconds.
	// There is no real penalty for making this bigger, because all it does is kill the channel when it expires,
	// so upping this to 12 seconds.
	{ ConfigurationKey tmp("GSM.Timer.T3103","12000",
		"milliseconds",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::VALRANGE,
		"2000:30000(100)",
		false,	// not static
		"Handover timeout in milliseconds, GSM 04.08 11.1.2.  "
			"This is the timeout for a handset to seize a channel during handover."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("GSM.Timer.T3105","50",
		"milliseconds",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::VALRANGE,
		"25:75(5)",// educated guess
		false,	// not static
		"Milliseconds for handset to respond to physical information.  "
			"GSM 04.08 11.1.2."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("GSM.Timer.T3109","30000",
		"milliseconds",
		ConfigurationKey::CUSTOMERWARN,
		ConfigurationKey::VALRANGE,
		"5000:60000",
		false,	// not static, updated each time channel is opened.
		"When a channel is released or contact with a handset is lost, the channel is deactivated for this period of time before being reused.  "
			"This time must be longer than the time indicated by GSM.CellOptions.RADIO-LINK-TIMEOUT.  "
			"GSM 04.08 11.1.2.  "
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("GSM.Timer.T3113","10000",
		"milliseconds",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::VALRANGE,
		"5000:15000(500)",// educated guess
		false,
		"Paging timer T3113 in milliseconds.  "
			"This is the timeout for a handset to respond to a paging request.  "
			"This should usually be the same as SIP.Timer.B in your VoIP network."
	);
	map[tmp.getName()] = tmp;
	}

	// (pat) It is unfortunate that this is specified in msecs.
	//{ ConfigurationKey tmp("GSM.Timer.T3122Max","255000",
	//	"milliseconds",
	//	ConfigurationKey::DEVELOPER,
	//	ConfigurationKey::VALRANGE,
	//	"10000:255000(1000)",
	//	false,
	//	"Maximum allowed value for T3122, the RACH holdoff timer, in milliseconds.  "
	//	"This timer is sent to the MS with a granularity of seconds in the range 1-255.  GSM 4.08 10.5.2.43."
	//);
	//map[tmp.getName()] = tmp;
	//}

	//// (pat) It is unfortunate that this is specified in msecs.
	//{ ConfigurationKey tmp("GSM.Timer.T3122Min","10000",	// 2-2014: Pat upped from 2 to 10 seconds.
	//	"milliseconds",
	//	ConfigurationKey::DEVELOPER,
	//	ConfigurationKey::VALRANGE,
	//	"1000:255000(1000)",
	//	false,
	//	"Minimum allowed value for T3122, the RACH holdoff timer, in milliseconds.  "
	//	"GSM 4.08 10.5.2.43. This timer is sent to the MS with a granularity of seconds in the range 1-255.  "
	//	"The purpose is to postpone the MS RACH procedure until an SDCCH available, so there is no point making it any smaller than "
	//	"the expected availability of the SDCCH, which will take several seconds."
	//);
	//map[tmp.getName()] = tmp;
	//}

	{ ConfigurationKey tmp("GSM.Timer.T3212","0",
		"minutes",
		ConfigurationKey::CUSTOMERTUNE,
		ConfigurationKey::VALRANGE,
		"0:1530(6)",
		false,
		"Registration timer T3212 period in minutes.  "
			"Should be a factor of 6.  "
			"Set to 0 to disable periodic registration.  "
			"Should be smaller than SIP registration period."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("NodeManager.API.PhysicalStatus","disabled",
		"version",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::CHOICE,
		"disabled,"
			"0.1",
		false,
		"Which version of the PhysicalStatus event stream should be enabled."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("NodeManager.Commands.Port","45060",
		"",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::PORT,
		"",
		true,
		"Port used by the NodeManager to receive and respond to JSON formatted commands.  "
		"Some examples of the available commands and their formats are available in NodeManager/JSON_Interface.txt."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("NodeManager.Events.Port","45160",
		"",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::PORT,
		"",
		true,
		"Port used by the NodeManager to publish API events."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("Peering.Neighbor.RefreshAge","60",
		"seconds",
		ConfigurationKey::CUSTOMERTUNE,
		ConfigurationKey::VALRANGE,
		"10:3600",// educated guess
		false,
		"Seconds before refreshing parameters from a neighbor."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("Peering.NeighborTable.Path","/var/run/NeighborTable.db",
		"",
		ConfigurationKey::CUSTOMERWARN,
		ConfigurationKey::FILEPATH,
		"",
		true,
		"File path for neighbor information database."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("Peering.Port","16001",
		"",
		ConfigurationKey::CUSTOMERWARN,
		ConfigurationKey::PORT,
		"",
		true,
		"The UDP port used by the peer interface for handover."
	);
	map[tmp.getName()] = tmp;
	}

	// (pat) These Peering params will go away entirely when we switch the Peer interface to SIP.
	// (pat) The product of ResendTimeout and ResendCount needs to be larger than twice the maximum possible internet rount-trip delay.
	// (pat) It does not hurt to send extra messages.
	{ ConfigurationKey tmp("Peering.ResendCount","20",
		"attempts",
		ConfigurationKey::DEVELOPER, // (pat) This Peering params should not be customer tunable.
		ConfigurationKey::VALRANGE,
		"3:40",// educated guess
		false,
		"Number of tries to send message over the peer interface before giving up."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("Peering.ResendTimeout","100",
		"milliseconds",
		ConfigurationKey::DEVELOPER, // (pat) This Peering params should not be customer tunable.
		ConfigurationKey::VALRANGE,
		"50:1000(10)",// educated guess
		false,
		"Milliseconds before resending a message on the peer interface."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("RTP.Range","98",
		"ports",
		ConfigurationKey::CUSTOMERTUNE,
		ConfigurationKey::VALRANGE,
		"25:200",// educated guess
		true,
		"Range of RTP port pool.  "
			"Pool is RTP.Start to RTP.Range - 1."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("RTP.Start","16484",
		"",
		ConfigurationKey::CUSTOMERTUNE,
		ConfigurationKey::PORT,
		"",
		true,
		"Base of RTP port pool.  "
			"Pool is RTP.Start to RTP.Range - 1."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("SGSN.Debug","0",
		"",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::BOOLEAN,
		"",
		false,
		"Add layer 3 messages to the GGSN.Logfile, if any."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("SGSN.Timer.ImplicitDetach","3480",
		"seconds",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::VALRANGE,
		"2000:4000(10)",// educated guess
		false,
		"3GPP 24.008 11.2.2.  "
			"GPRS attached MS is implicitly detached in seconds.  "
			"Should be at least 240 seconds greater than SGSN.Timer.RAUpdate.");
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("SGSN.Timer.MS.Idle","600",
		"?seconds",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::VALRANGE,
		"300:900(10)",// educated guess
		false,
		"How long an MS is idle before the SGSN forgets TLLI specific information."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("SGSN.Timer.RAUpdate","0",
		"seconds",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::VALRANGE,
		"0:11160(2)",	// (pat) 0 deactivates, up to 31 deci-hours which is 11160 seconds.  Minimum increment is 2 seconds.
		false,
		"Also known as T3312, 3GPP 24.008 4.7.2.2.  "
			"How often MS reports into the SGSN when it is idle, in seconds.  "
			"Setting to 0 or >12000 deactivates entirely, i.e., sets the timer to effective infinity.  "
			"Note: to prevent GPRS Routing Area Updates you must set both this and GSM.Timer.T3212 to 0."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("SGSN.Timer.Ready","44",
		"seconds",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::VALRANGE,
		"30:90",// educated guess
		false,
		"Also known as T3314, 3GPP 24.008 4.7.2.1.  "
			"Inactivity period required before MS may perform another routing area or cell update, in seconds."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("SIP.DTMF.RFC2833","1",
		"",
		ConfigurationKey::CUSTOMERWARN,
		ConfigurationKey::BOOLEAN,
		"",
		false,
		"Use RFC-2833 (RTP event signalling) for in-call DTMF."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("SIP.DTMF.RFC2833.PayloadType","101",
		"",
		ConfigurationKey::CUSTOMERWARN,
		ConfigurationKey::VALRANGE,
		"96:127",
		false,
		"Payload type to use for RFC-2833 telephone event packets."
	);
	map[tmp.getName()] = tmp;
	}

	// (pat) Geez, it is RFC2976, not 2967, and has been replaced by RFC6086
	// I am deprecating this, but I did not feel I could remove it because it might be in use in existing
	// configs, so I marked it developer only.
	{ ConfigurationKey tmp("SIP.DTMF.RFC2967","0",
		"",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::BOOLEAN,
		"",
		false,
		"Obsolete; incorrect RFC number.  Use SIP.DTMF.RFC2976."
	);
	map[tmp.getName()] = tmp;
	}

	// Note that RFC2976 is deprecated by RFC6086.  These discuss SIP INFO in general, not DTMF specifically.
	{ ConfigurationKey tmp("SIP.DTMF.RFC2976","0",
		"",
		ConfigurationKey::CUSTOMERWARN,
		ConfigurationKey::BOOLEAN,
		"",
		false,
		"Use RFC-2976 (SIP INFO method) for in-call DTMF."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("SIP.Local.IP","127.0.0.1",
		"",
		ConfigurationKey::CUSTOMERWARN,
		ConfigurationKey::IPADDRESS,
		"",
		true,
		"IP address of the OpenBTS machine as seen by its proxies.  "
			"If these are all local, this can be localhost.",
		ConfigurationKey::NODESPECIFIC
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("SIP.Local.Port","5062",
		"",
		ConfigurationKey::CUSTOMERWARN,
		ConfigurationKey::PORT,
		"",
		true,
		"IP port that OpenBTS uses for its SIP interface."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("SIP.MaxForwards","70",
		"referrals",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::VALRANGE,
		"1:100",
		false,
		"Maximum allowed number of referrals."
	);
	map[tmp.getName()] = tmp;
	}


	// (pat) 5-1-2013
	{ ConfigurationKey tmp("SIP.Proxy.Mode","",	// name and default value
		"",		// units
		ConfigurationKey::DEVELOPER,	// visiblity
		ConfigurationKey::CHOICE_OPT,	// type
		"direct",					// validation choices.  These are comma separated
		false,		// is static?
		"If set to direct, then direct BTS to BTS calls are permitted without an intervening SIP switch, for example, no asterisk needed."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("SIP.Proxy.Registration","127.0.0.1:5064",
		"",
		ConfigurationKey::CUSTOMERWARN,
		ConfigurationKey::HOSTANDPORT,
		"",
		false,
		"The hostname or IP address and port of the proxy to be used for registration and authentication.  "
			"This should normally be the subscriber registry SIP interface, not Asterisk.",
		ConfigurationKey::GLOBALLYSAME
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("SIP.Proxy.SMS","127.0.0.1:5063",
		"",
		ConfigurationKey::CUSTOMERWARN,
		ConfigurationKey::HOSTANDPORT,
		"",
		false,
		"The hostname or IP address and port of the proxy to be used for text messaging.  "
			"This is smqueue, for example.",
		ConfigurationKey::GLOBALLYSAME
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("SIP.Proxy.Speech","127.0.0.1:5060",
		"",
		ConfigurationKey::CUSTOMERWARN,
		ConfigurationKey::HOSTANDPORT,
		"",
		false,
		"The hostname or IP address and port of the proxy to be used for normal speech calls.  "
			"This is Asterisk, for example.",
		ConfigurationKey::GLOBALLYSAME
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("SIP.Proxy.USSD","",
		"",
		ConfigurationKey::CUSTOMERWARN,
		ConfigurationKey::REGEX_OPT,
		"^[:0-9a-zA-Z_.~-]+:[0-9]+$|^testmode$",		// The ":" is for IPv6.
		false,
		"The hostname or IP address and port of the proxy to be used for USSD, "
			"or \"testmode\" to test by reflecting USSD messages back to the handset.  "
			"To disable USSD, execute \"unconfig SIP.Proxy.USSD\".",
		ConfigurationKey::GLOBALLYSAME
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("SIP.Realm","",
		"",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::STRING_OPT,
		"^[0-9a-zA-Z_.-]",
		false,
		"SIP Realm for interop with certain switches. Filling in a host here will also activate an new REGISTER auth method."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("SIP.RegistrationPeriod","90",
		"minutes",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::VALRANGE,
		"6:2298",// educated guess
		false,
		"Registration period in minutes for MS SIP users.  "
			"Should be longer than GSM T3212."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("SIP.RFC3428.NoTrying","0",
		"",
		ConfigurationKey::CUSTOMERWARN,
		ConfigurationKey::BOOLEAN,
		"",
		false,
		"Send \"100 Trying\" response to SIP MESSAGE, even though that violates RFC-3428."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("SIP.SMSC","smsc",
		"",
		ConfigurationKey::CUSTOMERWARN,
		ConfigurationKey::CHOICE_OPT,// audited
		"smsc",
		false,
		"The SMSC handler in smqueue.  "
			"This is the entity that handles full 3GPP MIME-encapsulted TPDUs.  "
			"If not defined, use direct numeric addressing.  "
			"The value should be disabled with \"unconfig SIP.SMSC\" if SMS.MIMEType is \"text/plain\" or set to \"smsc\" if SMS.MIMEType is \"application/vnd.3gpp\".",
		ConfigurationKey::GLOBALLYSAME
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("SIP.Timer.A","2000",
		"milliseconds",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::VALRANGE,
		"1500:2500(100)",// educated guess
		false,
		"SIP timer A, the INVITE retry period, RFC-3261 Section 17.1.1.2, in milliseconds."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("SIP.Timer.B","10000",
		"milliseconds",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::VALRANGE,
		"5000:15000(100)",// educated guess
		false,
		"INVITE transaction timeout in milliseconds.  "
			"This value should usually match GSM.Timer.T3113."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("SIP.Timer.E","500",
		"milliseconds",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::VALRANGE,
		"250:750(10)",// educated guess
		false,
		"Non-INVITE initial request retransmit period in milliseconds."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("SIP.Timer.F","5000",
		"milliseconds",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::VALRANGE,
		"2500:7500(100)",// educated guess
		false,
		"Non-INVITE initial request timeout in milliseconds."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("SIP.Timer.H","5000",
		"milliseconds",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::VALRANGE,
		"2500:7500(100)",// educated guess
		false,
		"ACK timeout period in milliseconds."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("SMS.FakeSrcSMSC","0000",
		"",
		ConfigurationKey::CUSTOMERWARN,
		ConfigurationKey::STRING,
		"^[0-9]+$",
		false,
		"Use this to fill in L4 SMSC address in SMS delivery."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("SMS.MIMEType","application/vnd.3gpp.sms",
		"",
		ConfigurationKey::CUSTOMERWARN,
		ConfigurationKey::CHOICE,
		"application/vnd.3gpp.sms,"
			"text/plain",
		false,
		"This is the MIME Type that OpenBTS will use for RFC-3428 SIP MESSAGE payloads.  "
			"Valid values are \"application/vnd.3gpp.sms\" and \"text/plain\".",
		ConfigurationKey::GLOBALLYSAME
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("TRX.IP","127.0.0.1",
		"",
		ConfigurationKey::CUSTOMERWARN,
		ConfigurationKey::IPADDRESS,
		"",
		true,
		"IP address of the transceiver application."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("TRX.MinimumRxRSSI","-63",
		"dB",
		ConfigurationKey::FACTORY,
		ConfigurationKey::VALRANGE,
		"-90:90",// educated guess
		false,
		"Bursts received at the physical layer below this threshold are automatically ignored.  "
			"Values in dB.  "
			"Set at the factory.  "
			"Do not adjust without proper calibration.",
		ConfigurationKey::NODESPECIFIC
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("TRX.Port","5700",
		"",
		ConfigurationKey::FACTORY,
		ConfigurationKey::PORT,
		"",
		true,
		"IP port of the transceiver application."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("TRX.RadioFrequencyOffset","128",
		"~170Hz steps",
		ConfigurationKey::FACTORY,
		ConfigurationKey::VALRANGE,
		"64:192",
		true,
		"Fine-tuning adjustment for the Transceiver master clock.  "
			"Roughly 170 Hz/step.  "
			"Set at the factory.  "
			"Do not adjust without proper calibration.",
		ConfigurationKey::NODESPECIFIC
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("TRX.Timeout.Clock","10",
		"seconds",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::VALRANGE,
		"5:15",// educated guess
		false,
		"How long to wait during a read operation from the Transceiver before giving up."
	);
	map[tmp.getName()] = tmp;
	}

	// unused?
	{ ConfigurationKey tmp("TRX.Timeout.Start","2",
		"seconds",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::VALRANGE,
		"1:3",// educated guess
		false,
		"How long to wait during system startup before checking to see if the Transceiver can be reached."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("TRX.TxAttenOffset","0",
		"dB of attenuation",
		ConfigurationKey::FACTORY,
		ConfigurationKey::VALRANGE,
		"0:100",// educated guess
		true,
		"Hardware-specific gain adjustment for transmitter, matched to the power amplifier, expessed as an attenuation in dB.  "
			"Set at the factory.  "
			"Do not adjust without proper calibration.",
		ConfigurationKey::NODESPECIFIC
	);
	map[tmp.getName()] = tmp;
	}

#if 0
	//kurtis
	// (pat 3-2014) Removed.  This is a great idea that cannot work yet in the transceiver because of the order of initialization
	// dictated by the gConfig implementation.   Furthermore the code in OpenBTS.cpp s botched - it neither parses this for multiple
	// arguments nor passes it properly, and rather than fix it I am just removing pending a demonstrated need.
	{ ConfigurationKey tmp("TRX.Args","",
		"",
		ConfigurationKey::CUSTOMER,
		ConfigurationKey::STRING,
		"",
		false,
		"Extra arguments for the Transceiver."
	);
	map[tmp.getName()] = tmp;
	}
#endif

	{ ConfigurationKey tmp("Test.GSM.SimulatedFER.Downlink","0",
		"probability in %",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::VALRANGE,
		"0:100(5)",// educated guess
		false,	// this option takes effect immediately
		"Probability (0-100) of dropping any downlink frame to test robustness."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("Test.GSM.SimulatedFER.Uplink","0",
		"probability in %",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::VALRANGE,
		"0:100(5)",// educated guess
		false,	// this option takes effect immediately
		"Probability (0-100) of dropping any uplink frame to test robustness."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("Test.GSM.UplinkFuzzingRate","0",
		"probability in %",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::VALRANGE,
		"0:100(5)",// educated guess
		true,
		"Probability (0-100) of flipping a bit in any uplink frame to test robustness."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("Test.SIP.SimulatedPacketLoss","0",
		"probability in %",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::VALRANGE,
		"0:100(5)",// educated guess
		true,
		"Probability (0-100) of dropping any inbound or outbound SIP packet to test robustness."
	);
	map[tmp.getName()] = tmp;
	}

	{ ConfigurationKey tmp("Control.CDR.Dirname","", // (pat) Added 7-2014.
		"filename",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::FILEPATH_OPT,
		"",
		true,
		"If set, CDR (Call Data Records) are placed in this directory.  A good choice is /var/log/OpenBTS.  "
	);
	map[tmp.getName()] = tmp;
	}

	return map;
}


void OpenBTSConfig::configUpdateKeys()
{
	// (pat) Doesnt hurt to use float (unless overflow) for integer keys, but not vice versa.
#define SAVE_NUMERIC_KEY(keyname) { keyname = getFloat(#keyname); } 		// if (defines(#keyname)) { keyname = getNum(#keyname); }
	SAVE_NUMERIC_KEY(GSM.Handover.FailureHoldoff);
	SAVE_NUMERIC_KEY(GSM.Handover.Margin);
	SAVE_NUMERIC_KEY(GSM.Handover.Ny1);

	SAVE_NUMERIC_KEY(GSM.Handover.History.Max);
	//not implemented: SAVE_NUMERIC_KEY(GSM.Handover.Penalty.Damping);

	SAVE_NUMERIC_KEY(GSM.Handover.RXLEV_DL.Target);
	SAVE_NUMERIC_KEY(GSM.Handover.RXLEV_DL.History);
	SAVE_NUMERIC_KEY(GSM.Handover.RXLEV_DL.Margin);
	SAVE_NUMERIC_KEY(GSM.Handover.RXLEV_DL.PenaltyTime);

	SAVE_NUMERIC_KEY(GSM.MS.Power.Min);
	SAVE_NUMERIC_KEY(GSM.MS.Power.Max);
	SAVE_NUMERIC_KEY(GSM.MS.Power.Damping);

	SAVE_NUMERIC_KEY(GSM.MS.TA.Damping);
	SAVE_NUMERIC_KEY(GSM.MS.TA.Max);

	SAVE_NUMERIC_KEY(GSM.Timer.T3103);
	SAVE_NUMERIC_KEY(GSM.Timer.T3105);
	SAVE_NUMERIC_KEY(GSM.Timer.T3109);
	SAVE_NUMERIC_KEY(GSM.Timer.T3113);
	SAVE_NUMERIC_KEY(GSM.Timer.T3212);
	SAVE_NUMERIC_KEY(GSM.BTS.RADIO_LINK_TIMEOUT);
}
