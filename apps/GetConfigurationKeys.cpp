/*
* Copyright 2008, 2009, 2010 Free Software Foundation, Inc.
* Copyright 2010 Kestrel Signal Processing, Inc.
* Copyright 2011, 2012 Range Networks, Inc.
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

#include <iostream>
#include <fstream>
#include <vector>
#include <string>

#include <Configuration.h>

ConfigurationKeyMap getConfigurationKeys()
{

	//VALIDVALUES NOTATION:
	// * A:B : range from A to B in steps of 1
	// * A:B(C) : range from A to B in steps of C
	// * A:B,D:E : range from A to B and from D to E
	// * A,B : multiple choices of value A and B
	// * X|A,Y|B : multiple choices of string "A" with value X and string "B" with value Y
	// * ^REGEX$ : string must match regular expression

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
	ConfigurationKey *tmp;

	tmp = new ConfigurationKey("CLI.SocketPath","/var/run/command",
		"",
		ConfigurationKey::CUSTOMERWARN,
		ConfigurationKey::FILEPATH,
		"",
		false,
		"Path for Unix domain datagram socket used for the OpenBTS console interface."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("Control.Call.QueryRRLP.Early","0",
		"",
		ConfigurationKey::CUSTOMER,
		ConfigurationKey::BOOLEAN,
		"",
		false,
		"Query every MS for its location via RRLP during the setup of a call."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("Control.Call.QueryRRLP.Late","0",
		"",
		ConfigurationKey::CUSTOMER,
		ConfigurationKey::BOOLEAN,
		"",
		false,
		"Query every MS for its location via RRLP during the teardown of a call."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("Control.GSMTAP.GPRS","0",
		"",
		ConfigurationKey::CUSTOMERWARN,
		ConfigurationKey::BOOLEAN,
		"",
		false,
		"Capture GPRS signaling and traffic at L1/L2 interface via GSMTAP."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("Control.GSMTAP.GSM","0",
		"",
		ConfigurationKey::CUSTOMERWARN,
		ConfigurationKey::BOOLEAN,
		"",
		false,
		"Capture GSM signaling at L1/L2 interface via GSMTAP."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("Control.GSMTAP.TargetIP","127.0.0.1",
		"",
		ConfigurationKey::CUSTOMERWARN,
		ConfigurationKey::IPADDRESS,
		"",
		false,
		"Target IP address for GSMTAP packets; the IP address of Wireshark, if you use it for real time traces."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("Control.LUR.AttachDetach","1",
		"",
		ConfigurationKey::CUSTOMER,
		ConfigurationKey::BOOLEAN,
		"",
		false,
		"Use attach/detach procedure.  "
			"This will make initial LUR more prompt.  "
			"It will also cause an un-regstration if the handset powers off and really heavy LUR loads in areas with spotty coverage."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("Control.LUR.FailedRegistration.Message","Your handset is not provisioned for this network. ",
		"",
		ConfigurationKey::CUSTOMER,
		ConfigurationKey::STRING_OPT,// audited
		"^[[:print:]]+$",
		false,
		"Send this text message, followed by the IMSI, to unprovisioned handsets that are denied registration."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("Control.LUR.FailedRegistration.ShortCode","1000",
		"",
		ConfigurationKey::CUSTOMER,
		ConfigurationKey::STRING,
		"^[0-9]+$",
		false,
		"The return address for the failed registration message."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("Control.LUR.NormalRegistration.Message","",
		"",
		ConfigurationKey::CUSTOMER,
		ConfigurationKey::STRING_OPT,// audited
		"^[[:print:]]+$",
		false,
		"The text message (followed by the IMSI) to be sent to provisioned handsets when they attach on Um.  "
			"By default, no message is sent.  "
			"To have a message sent, specify one.  "
			"To stop sending messages again, execute \"unconfig Control.LUR.NormalRegistration.Message\"."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("Control.LUR.NormalRegistration.ShortCode","0000",
		"",
		ConfigurationKey::CUSTOMER,
		ConfigurationKey::STRING,
		"^[0-9]+$",
		false,
		"The return address for the normal registration message."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("Control.LUR.OpenRegistration","",
		"",
		ConfigurationKey::CUSTOMERWARN,
		ConfigurationKey::REGEX_OPT,// audited
		"",
		false,
		"This is value is a regular expression.  "
			"Any handset with an IMSI matching the regular expression is allowed to register, even if it is not provisioned.  "
			"By default, this feature is disabled.  "
			"To enable open registration, specify a regular expression e.g. ^460 (which matches any IMSI starting with 460, the MCC for China).  "
			"To disable open registration again, execute \"unconfig Control.LUR.OpenRegistration\"."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("Control.LUR.OpenRegistration.Message","Welcome to the test network.  Your IMSI is ",
		"",
		ConfigurationKey::CUSTOMER,
		ConfigurationKey::STRING,
		"^[[:print:]]+$",
		false,
		"Send this text message, followed by the IMSI, to unprovisioned handsets when they attach on Um due to open registration."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("Control.LUR.OpenRegistration.Reject","",
		"",
		ConfigurationKey::CUSTOMERWARN,
		ConfigurationKey::REGEX_OPT,// audited
		"",
		false,
		"This is value is a regular expression.  "
			"Any unprovisioned handset with an IMSI matching the regular expression is rejected for registration, even if it matches Control.LUR.OpenRegistration.  "
			"By default, this feature is disabled.  "
			"To enable this filter, specify a regular expression e.g. ^460 (which matches any IMSI starting with 460, the MCC for China).  "
			"To disable this filter again, execute \"unconfig Control.LUR.OpenRegistration.Reject\".  "
			"If Control.LUR.OpenRegistration is disabled, this parameter has no effect."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("Control.LUR.OpenRegistration.ShortCode","101",
		"",
		ConfigurationKey::CUSTOMER,
		ConfigurationKey::STRING,
		"^[0-9]+$",
		false,
		"The return address for the open registration message."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("Control.LUR.QueryClassmark","0",
		"",
		ConfigurationKey::CUSTOMER,
		ConfigurationKey::BOOLEAN,
		"",
		false,
		"Query every MS for classmark during LUR."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("Control.LUR.QueryIMEI","0",
		"",
		ConfigurationKey::CUSTOMER,
		ConfigurationKey::BOOLEAN,
		"",
		false,
		"Query every MS for IMEI during LUR."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("Control.LUR.QueryRRLP","0",
		"",
		ConfigurationKey::CUSTOMER,
		ConfigurationKey::BOOLEAN,
		"",
		false,
		"Query every MS for its location via RRLP during LUR."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("Control.LUR.SendTMSIs","0",
		"",
		ConfigurationKey::CUSTOMER,
		ConfigurationKey::BOOLEAN,
		"",
		false,
		"Send new TMSI assignments to handsets that are allowed to attach."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("Control.LUR.UnprovisionedRejectCause","0x04",
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
		"Reject cause for location updating failures for unprovisioned phones.  "
			"Reject causes come from GSM 04.08 10.5.3.6.  "
			"Reject cause 0x04, IMSI not in VLR, is usually the right one."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("Control.LUR.WhiteList","0",
		"",
		ConfigurationKey::CUSTOMER,
		ConfigurationKey::BOOLEAN,
		"",
		false,
		"Whitelist checking is performed."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("Control.LUR.WhiteListing.Message","Your handset needs to be added to the whitelist.",
		"",
		ConfigurationKey::CUSTOMER,
		ConfigurationKey::STRING,
		"^[[:print:]]+$",
		false,
		"The whitelisting notification message."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("Control.LUR.WhiteListing.RejectCause","0x04",
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
		"Reject cause for handset not in the whitelist, when whitelisting is enforced.  "
			"Reject causes come from GSM 04.08 10.5.3.6.  "
			"Reject cause 0x04, IMSI not in VLR, is usually the right one."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("Control.LUR.WhiteListing.ShortCode","1000",
		"",
		ConfigurationKey::CUSTOMER,
		ConfigurationKey::STRING,
		"^[0-9]+$",
		false,
		"The return address for the whitelisting notificiation message."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("Control.Reporting.PhysStatusTable","/var/run/ChannelTable.db",
		"",
		ConfigurationKey::CUSTOMERWARN,
		ConfigurationKey::FILEPATH,
		"",
		true,
		"File path for channel status reporting database."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("Control.Reporting.StatsTable","/var/log/OpenBTSStats.db",
		"",
		ConfigurationKey::CUSTOMERWARN,
		ConfigurationKey::FILEPATH,
		"",
		true,
		"File path for statistics reporting database."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("Control.Reporting.TMSITable","/var/run/TMSITable.db",
		"",
		ConfigurationKey::CUSTOMERWARN,
		ConfigurationKey::FILEPATH,
		"",
		true,
		"File path for TMSITable database."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("Control.Reporting.TransactionTable","/var/run/TransactionTable.db",
		"",
		ConfigurationKey::CUSTOMERWARN,
		ConfigurationKey::FILEPATH,
		"",
		true,
		"File path for transaction table database."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("Control.SACCHTimeout.BumpDown","1",
		"dB",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::VALRANGE,
		"1:3",// educated guess
		false,
		"Decrease the RSSI by this amount to induce more power in the MS each time we fail to receive a response from it."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("Control.SMS.QueryRRLP","0",
		"",
		ConfigurationKey::CUSTOMER,
		ConfigurationKey::BOOLEAN,
		"",
		false,
		"Query every MS for its location via RRLP during an SMS."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("Control.SMSCB.Table","",
		"",
		ConfigurationKey::CUSTOMERWARN,
		ConfigurationKey::FILEPATH_OPT,// audited
		"",
		true,
		"File path for SMSCB scheduling database.  "
			"By default, this feature is disabled.  "
			"To enable, specify a file path for the database e.g. /var/run/SMSCB.db.  "
			"To disable again, execute \"unconfig Control.SMSCB.Table\"."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("Control.TestCall.AutomaticModeChange","0",
		"",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::BOOLEAN,
		"",
		false,
		"Automatically change the channel mode of a TCH/FACCH from signaling-only to speech-V1 before starting the fuzzing interface."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("Control.TestCall.LocalPort","24020",
		"",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::PORT,
		"",
		false,
		"Port number part of source for L3 payloads received from the handset in fuzzing interface."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("Control.TestCall.PollTime","100",
		"milliseconds",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::VALRANGE,
		"50:200(10)",// educated guess
		false,
		"Polling time of the fuzzing interface in milliseconds."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("Control.TestCall.RemoteHost","127.0.0.1",
		"",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::IPADDRESS,
		"",
		false,
		"Host part of destination for L3 payloads received from the handset in fuzzing interface."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("Control.TestCall.RemotePort","24021",
		"",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::PORT,
		"",
		false,
		"Port number part of destination for L3 payloads received from the handset in fuzzing interface."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("Control.VEA","0",
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
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("Control.WatchdogMinutes","60",
		"minutes",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::VALRANGE,
		"0:6000",// educated guess
		false,
		"Number of minutes before the radio watchdog expires and OpenBTS is restarted."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("GGSN.DNS","",
		"",
		ConfigurationKey::CUSTOMERWARN,
		ConfigurationKey::MIPADDRESS_OPT,// audited
		"",
		true,
		"The list of DNS servers to be used by downstream clients.  "
			"By default, DNS servers are taken from the host system.  "
			"To override, specify a space-separated list of the DNS servers, in IP dotted notation, eg: 1.2.3.4 5.6.7.8.  "
			"To use the host system DNS servers again, execute \"unconfig GGSN.DNS\"."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("GGSN.Firewall.Enable","1",
		"",
		ConfigurationKey::CUSTOMERWARN,
		ConfigurationKey::CHOICE,
		"0|Disable Firewall,"
			"1|Block MS Access to OpenBTS and Other MS,"
			"2|Block All Private IP Addresses",
		true,
		"0=no firewall; 1=block MS attempted access to OpenBTS or other MS; 2=block all private IP addresses."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("GGSN.IP.MaxPacketSize","1520",
		"bytes",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::VALRANGE,
		"1492:9000",// educated guess
		true,
		"Maximum size of an IP packet.  "
			"Should normally be 1520."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("GGSN.IP.ReuseTimeout","180",
		"seconds",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::VALRANGE,
		"120:240",// educated guess,
		true,
		"How long IP addresses are reserved after a session ends."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("GGSN.IP.TossDuplicatePackets","0",
		"",
		ConfigurationKey::CUSTOMER,
		ConfigurationKey::BOOLEAN,
		"",
		true,
		"Toss duplicate TCP/IP packets to prevent unnecessary traffic on the radio."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("GGSN.Logfile.Name","",
		"",
		ConfigurationKey::FACTORY,
		ConfigurationKey::FILEPATH_OPT,
		"",
		true,
		"If specified, internet traffic is logged to this file e.g. ggsn.log"
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("GGSN.MS.IP.Base","192.168.99.1",
		"",
		ConfigurationKey::CUSTOMERTUNE,
		ConfigurationKey::IPADDRESS,
		"",
		true,
		"Base IP address assigned to MS."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("GGSN.MS.IP.MaxCount","254",
		"addresses",
		ConfigurationKey::CUSTOMERTUNE,
		ConfigurationKey::VALRANGE,
		"1:254",// educated guess
		true,
		"Number of IP addresses to use for MS."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("GGSN.MS.IP.Route","",
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
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("GGSN.ShellScript","",
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
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("GGSN.TunName","sgsntun",
		"",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::STRING,
		"^[a-z0-9]+$",
		true,
		"Tunnel device name for GGSN."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("GPRS.advanceblocks","10",
		"blocks",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::VALRANGE,
		"5:15",// educated guess
		false,
		"Number of advance blocks to use in the CCCH reservation."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("GPRS.CellOptions.T3168Code","5",
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
				"Range 0..7 to represent 0.5sec to 4sec in 0.5sec steps."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("GPRS.CellOptions.T3192Code","0",
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
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("GPRS.ChannelCodingControl.RSSI","-40",
		"dB",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::VALRANGE,
		"-65:-15",// educated guess
		false,
		"If the initial unlink signal strength is less than this amount in DB GPRS uses a lower bandwidth but more robust encoding CS-1.  "
			"This value should normally be GSM.Radio.RSSITarget + 10 dB."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("GPRS.Channels.Congestion.Threshold","200",
		"probability in %",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::VALRANGE,
		"100:300(5)",// educated guess
		false,
		"The GPRS channel is considered congested if the desired bandwidth exceeds available bandwidth by this amount, specified in percent."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("GPRS.Channels.Congestion.Timer","60",
		"seconds",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::VALRANGE,
		"30:90(5)",// educated guess
		false,
		"How long in seconds GPRS congestion exceeds the Congestion.Threshold before we attempt to allocate another channel for GPRS."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("GPRS.Channels.Min.C0","2",
		"channels",
		ConfigurationKey::CUSTOMERTUNE,
		ConfigurationKey::VALRANGE,
		"0:7",
		false,
		"Minimum number of channels allocated for GPRS service on ARFCN C0."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("GPRS.Channels.Min.CN","0",
		"channels",
		ConfigurationKey::CUSTOMERTUNE,
		ConfigurationKey::VALRANGE,
		"0:100",
		false,
		"Minimum number of channels allocated for GPRS service on ARFCNs other than C0."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

#if GPRS_CHANNELS_MAX_SUPPORTED
	tmp = new ConfigurationKey("GPRS.Channels.Max","4",
		"channels",
		ConfigurationKey::CUSTOMERTUNE,
		ConfigurationKey::VALRANGE,
		"0:10",// educated guess
		false,
		"Maximum number of channels allocated for GPRS service."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;
#endif

	tmp = new ConfigurationKey("GPRS.Codecs.Downlink","14",
		"",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::STRING,
		"^1{0,1}2{0,1}3{0,1}4{0,1}$",// "1234" with each number optional
		false,
		"List of allowed GPRS downlink codecs 1..4 for CS-1..CS-4.  Currently, only 1 and 4 are supported e.g. 14."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("GPRS.Codecs.Uplink","14",
		"",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::STRING,
		"^1{0,1}2{0,1}3{0,1}4{0,1}$",// "1234" with each number optional
		false,
		"List of allowed GPRS uplink codecs 1..4 for CS-1..CS-4.  Currently, only 1 and 4 are supported e.g. 14."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("GPRS.Counters.Assign","10",
		"messages",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::VALRANGE,
		"5:15",// educated guess
		false,
		"Maximum number of assign messages sent"
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("GPRS.Counters.N3101","20",
		"responses",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::VALRANGE,
		"8:32",// educated guess
		false,
		"Counts unused USF responses to detect nonresponsive MS.  "
			"Should be > 8.  "
			"See GSM04.60 sec 13."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("GPRS.Counters.N3103","8",
		"attempts",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::VALRANGE,
		"4:12",// educated guess
		false,
		"Counts ACK/NACK attempts to detect nonresponsive MS.  "
			"See GSM04.60 sec 13."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("GPRS.Counters.N3105","12",
		"responses",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::VALRANGE,
		"6:18",// educated guess
		false,
		"Counts unused RRBP responses to detect nonresponsive MS. "
			"See GSM04.60 sec 13."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("GPRS.Counters.Reassign","6",
		"messages",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::VALRANGE,
		"3:9",// educated guess
		false,
		"Maximum number of reassign messages sent"
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("GPRS.Counters.TbfRelease","5",
		"messages",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::VALRANGE,
		"3:8",// educated guess
		false,
		"Maximum number of TBF release messages sent"
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("GPRS.Debug","0",
		"",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::BOOLEAN,
		"",
		false,
		"Toggle GPRS debugging."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("GPRS.Downlink.KeepAlive","300",
		"milliseconds",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::VALRANGE,
		"200:5000(100)",// educated guess
		false,
		"How often to send keep-alive messages for persistent TBFs in milliseconds; must be long enough to avoid simultaneous in-flight duplicates, and short enough that MS gets one every 5 seconds.  "
			"GSM 5.08 10.2.2 indicates MS must get a block every 360ms"
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("GPRS.Downlink.Persist","0",
		"milliseconds",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::VALRANGE,
		"0:10000(100)",// educated guess
		false,
		"After completion, downlink TBFs are held open for this time in milliseconds.  "
			"If non-zero, must be greater than GPRS.Downlink.KeepAlive."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("GPRS.Enable","1",

		"",
		ConfigurationKey::CUSTOMER,
		ConfigurationKey::BOOLEAN,
		"",
		false,
		"If enabled, GPRS service is advertised in the C0T0 beacon, and GPRS service may be started on demand.  "
			"See also GPRS.Channels.*."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("GPRS.LocalTLLI.Enable","1",
		"",
		ConfigurationKey::CUSTOMERTUNE,
		ConfigurationKey::BOOLEAN,
		"",
		false,
		"Enable recognition of local TLLI"
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("GPRS.MS.KeepExpiredCount","20",
		"structs",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::VALRANGE,
		"10:30",// eduated guess
		false,
		"How many expired MS structs to retain; they can be viewed with gprs list ms -x"
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("GPRS.MS.Power.Alpha","10",
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
			"Determines sensitivity of handset to variations in downlink RSSI.  "
			"Valid range is 0...10 for alpha values of 0...1.0.  "
			"See GSM 05.08 10.2.1."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("GPRS.MS.Power.Gamma","31",
		"2 dB steps",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::VALRANGE,
		"0:31",
		false,
		"MS power control parameter, in 2 dB steps.  "
			"Determines baseline of handset uplink power relative to downlink RSSI. "
			"The optimum value will tend to be lower for BTS units with higher power output.  "
			"This default assumes a balanced link with a BTS output of 2-4 W/ARFCN.  "
			"Valid range is 0...31 for gamma values of 0...62 dB.  "
			"See GSM 05.08 10.2.1."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("GPRS.MS.Power.T_AVG_T","15",
		"",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::VALRANGE,
		"0:25",
		true,
		"MS power control parameter; see GSM 05.08 10.2.1."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("GPRS.MS.Power.T_AVG_W","15",
		"",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::VALRANGE,
		"0:25",
		true,
		"MS power control parameter; see GSM 05.08 10.2.1."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("GPRS.Multislot.Max.Downlink","3",
		"channels",
		ConfigurationKey::CUSTOMERTUNE,
		ConfigurationKey::VALRANGE,
		"0:10",// educated guess
		false,
		"Maximum number of channels used for a single MS in downlink."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("GPRS.Multislot.Max.Uplink","2",
		"channels",
		ConfigurationKey::CUSTOMERTUNE,
		ConfigurationKey::VALRANGE,
		"0:10",// educated guess
		false,
		"Maximum number of channels used for a single MS in uplink."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("GPRS.NC.NetworkControlOrder","2",
		"",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::VALRANGE,
		"0:3",
		true,
		"Controls measurement reports and cell reselection mode (MS autonomous or under network control); should not be changed. "
			"See GSM 5.08 10.1.4."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("GPRS.NMO","2",
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
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("GPRS.PRIORITY-ACCESS-THR","6",
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
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("GPRS.RAC","0",
		"",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::VALRANGE,
		"0:255",
		true,
		"GPRS Routing Area Code, advertised in the C0T0 beacon."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("GPRS.RA_COLOUR","0",
		"",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::VALRANGE,
		"0:7",
		false,
		"GPRS Routing Area Color as advertised in the C0T0 beacon."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("GPRS.RRBP.Min","0",
		"?",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::VALRANGE,
		"0:3",
		false,
		"Minimum value for RRBP reservations, range 0..3.  "
			"Should normally be 0.  "
			"A non-zero value gives the MS more time to respond to the RRBP request."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("GPRS.Reassign.Enable","1",
		"",
		ConfigurationKey::CUSTOMERTUNE,
		ConfigurationKey::BOOLEAN,
		"",
		false,
		"Enable TBF Reassignment"
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("GPRS.SendIdleFrames","0",
		"",
		ConfigurationKey::FACTORY,
		ConfigurationKey::BOOLEAN,
		"",
		false,
		"Should be 0 for current transceiver or 1 for deprecated version of transceiver."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("GPRS.SGSN.port","1920",
		"",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::PORT,
		"",
		false,
		"Port number of the SGSN required for GPRS service.  This must match the port specified in the SGSN config file, currently osmo_sgsn.cfg."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("GPRS.TBF.Downlink.Poll1","10",
		"blocks",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::VALRANGE,
		"5:15",// educated guess
		false,
		"When the first poll is sent for a downlink tbf, measured in blocks sent."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("GPRS.TBF.EST","1",
		"",
		ConfigurationKey::CUSTOMERTUNE,
		ConfigurationKey::BOOLEAN,
		"",
		false,
		"Allow MS to request another uplink assignment at end up of uplink TBF. "
			"See GSM 4.60 9.2.3.4"
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("GPRS.TBF.Expire","30000",
		"milliseconds",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::VALRANGE,
		"20000:40000",// educated guess
		false,
		"How long to try before giving up on a TBF."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("GPRS.TBF.KeepExpiredCount","20",
		"structs",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::VALRANGE,
		"15:25",// educated guess
		false,
		"How many expired TBF structs to retain; they can be viewed with gprs list tbf -x"
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("GPRS.TBF.Retry","1",
		"",
		ConfigurationKey::CUSTOMERTUNE,
		ConfigurationKey::CHOICE,
		"0|Do Not Retry,"
			"1|Codec 1,"
			"2|Codec 2,"
			"3|Codec 3,"
			"4|Codec 4",
		false,
		"If 0, no tbf retry, otherwise if a tbf fails it will be retried with this codec, numbered 1..4"
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("GPRS.Timers.Channels.Idle","6000",
		"milliseconds",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::VALRANGE,
		"3000:6000(100)",// educated guess
		false,
		"How long in milliseconds a GPRS channel is idle before being returned to the pool of channels.  "
			"Also depends on Channels.Min.  "
			"Currently the channel cannot be returned to the pool while there is any GPRS activity on any channel."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("GPRS.Timers.MS.Idle","600",
		"seconds",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::VALRANGE,
		"300:900(10)",// educated guess
		false,
		"How long in seconds an MS is idle before the BTS forgets about it."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("GPRS.Timers.MS.NonResponsive","6000",
		"milliseconds",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::VALRANGE,
		"3000:9000(100)",// educated guess
		false,
		"How long in milliseconds a TBF is non-responsive before the BTS kills it."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("GPRS.Timers.T3169","5000",
		"milliseconds",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::VALRANGE,
		"2500:7500(100)",// educated guess
		false,
		"Nonresponsive uplink TBF resource release timer, in milliseconds. "
			"See GSM04.60 sec 13."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("GPRS.Timers.T3191","5000",
		"milliseconds",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::VALRANGE,
		"2500:7500(100)",// educated guess
		false,
		"Nonresponsive downlink TBF resource release timer, in milliseconds. "
			"See GSM04.60 Section 13."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("GPRS.Timers.T3193","0",
		"milliseconds",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::VALRANGE,
		"0:5000(100)",// educated guess
		false,
		"Timer T3193 (in milliseconds) in the base station corresponds to T3192 in the MS, which is set by GPRS.CellOptions.T3192Code.  "
			"The T3193 value should be slightly longer than that specified by the T3192Code.  "
			"If 0, the BTS will fill in a default value based on T3192Code."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("GPRS.Timers.T3195","5000",
		"milliseconds",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::VALRANGE,
		"2500:7500(100)",// educated guess
		false,
		"Nonresponsive downlink TBF resource release timer, in milliseconds. "
			"See GSM04.60 Section 13."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("GPRS.Uplink.KeepAlive","300",
		"milliseconds",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::VALRANGE,
		"200:5000(100)",// educated guess
		false,
		"How often to send keep-alive messages for persistent TBFs in milliseconds; must be long enough to avoid simultaneous in-flight duplicates, and short enough that MS gets one every 5 seconds."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("GPRS.Uplink.Persist","4000",
		"milliseconds",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::VALRANGE,
		"0:6000(100)",// educated guess
		true,
		"After completion, uplink TBFs are held open for this time in milliseconds.  "
			"If non-zero, must be greater than GPRS.Uplink.KeepAlive. "
			"This is broadcast in the beacon and it cannot be changed once BTS is started."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("GSM.CCCH.AGCH.QMax","5",
		"queued access grants",
		ConfigurationKey::CUSTOMERTUNE,
		ConfigurationKey::VALRANGE,
		"3:8",// educated guess
		false,
		"Maximum number of access grants to be queued for transmission on AGCH before declaring congestion."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("GSM.CCCH.CCCH-CONF","1",
		"",
		ConfigurationKey::CUSTOMERTUNE,
		ConfigurationKey::CHOICE,
		"1|C-V Beacon,"
			"2|C-IV Beacon",
		true,
		"CCCH configuration type.  "
			"See GSM 10.5.2.11 for encoding.  "
			"Value of 1 means we are using a C-V beacon.  "
			"Any other value selects a C-IV beacon.  "
			"In C2.9 and earlier, the only allowed value is 1."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("GSM.CellOptions.RADIO-LINK-TIMEOUT","15",
		"seconds",
		ConfigurationKey::CUSTOMERTUNE,
		ConfigurationKey::VALRANGE,
		"10:20",// educated guess
		true,
		"Seconds before declaring a physical link dead."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("GSM.CellSelection.CELL-RESELECT-HYSTERESIS","3",
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
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("GSM.CellSelection.MS-TXPWR-MAX-CCH","0",
		"dB",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::VALRANGE,
		"0:31",
		false,
		"Cell selection parameters.  "
			"See GSM 04.08 10.5.2.4."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("GSM.CellSelection.NCCsPermitted","1",
		"",
		ConfigurationKey::CUSTOMERWARN,
		ConfigurationKey::VALRANGE,
		"0:255",
		false,
		"NCCs Permitted.  "
			"An 8-bit mask of allowed NCCs.  "
			"Unless you are coordinating with another carrier, this should probably just select your own NCC."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("GSM.CellSelection.NECI","1",
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
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("GSM.CellSelection.RXLEV-ACCESS-MIN","0",
		"dB",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::VALRANGE,
		"0:63",
		false,
		"Cell selection parameters.  "
			"See GSM 04.08 10.5.2.4."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("GSM.Channels.C1sFirst","0",
		"",
		ConfigurationKey::CUSTOMERTUNE,
		ConfigurationKey::BOOLEAN,
		"",
		true,
		"Allocate C-I slots first, starting at C0T1.  "
			"Otherwise, allocate C-VII slots first."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("GSM.Channels.NumC1s","7",
		"timeslots",
		ConfigurationKey::CUSTOMERTUNE,
		ConfigurationKey::VALRANGE,
		"0:100",// this is crap, calc from arfcns
		true,
		"Number of Combination-I timeslots to configure.  "
			"The C-I slot carries a single full-rate TCH, used for speech calling."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("GSM.Channels.NumC7s","0",
		"timeslots",
		ConfigurationKey::CUSTOMERTUNE,
		ConfigurationKey::VALRANGE,
		"0:100",// this is crap, calc from arfcns
		true,
		"Number of Combination-VII timeslots to configure.  "
			"The C-VII slot carries 8 SDCCHs, useful to handle high registration loads or SMS.  "
			"If C0T0 is C-IV, which it always is in C2.9 and earlier,, you must have at least one C-VII also."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("GSM.Channels.SDCCHReserve","0",
		"SDCCHs",
		ConfigurationKey::CUSTOMERTUNE,
		ConfigurationKey::VALRANGE,
		"0:10",// educated guess
		false,
		"Number of SDCCHs to reserve for non-LUR operations. "
			"This can be used to force LUR transactions into a lower priority."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("GSM.Cipher.CCHBER","0",
		"",
		ConfigurationKey::CUSTOMERWARN,
		ConfigurationKey::VALRANGE,
		"0.0:1.0",
		false,
		"Probability of a bit getting toggled in a control channel burst for cracking protection."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("GSM.Cipher.Encrypt","0",
		"",
		ConfigurationKey::CUSTOMERWARN,
		ConfigurationKey::BOOLEAN,
		"",
		false,
		"Encrypt traffic between phone and OpenBTS."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("GSM.Cipher.RandomNeighbor","0",
		"",
		ConfigurationKey::CUSTOMERWARN,
		ConfigurationKey::VALRANGE,
		"0.0:1.0",
		false,
		"Probability of a random neighbor being added to SI5 for cracking protection."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("GSM.Cipher.ScrambleFiller","0",
		"",
		ConfigurationKey::CUSTOMERWARN,
		ConfigurationKey::BOOLEAN,
		"",
		false,
		"Scramble filler in layer 2 for cracking protection."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("GSM.Control.GPRSMaxIgnore","5",
		"messages",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::VALRANGE,
		"3:8",// educated guess
		false,
		"Ignore GPRS messages on GSM control channels.  "
			"Value is number of consecutive messages to ignore."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("GSM.Handover.FailureHoldoff","5",
		"seconds",
		ConfigurationKey::CUSTOMERTUNE,
		ConfigurationKey::VALRANGE,
		"2:7",// educated guess
		false,
		"The number of seconds to wait before attempting another handover with a given neighbor BTS."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	// (pat) This value interacts with "GPRS.ChannelCodingControl.RSSI" which selects the GPRS codec.
	// In my opinion, if GPRS is enabled, we should handover to try to get the RSSI above GPRS.ChannelCodingControl.RSSI.
	tmp = new ConfigurationKey("GSM.Handover.LocalRSSIMin","-80",
		"dBm",
		ConfigurationKey::CUSTOMERTUNE,
		ConfigurationKey::VALRANGE,
		"-100:-60",// educated guess
		false,
		"Do not handover if downlink RSSI is above this level (in dBm), regardless of power difference."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("GSM.Handover.ThresholdDelta","10",
		"dB",
		ConfigurationKey::CUSTOMERTUNE,
		ConfigurationKey::VALRANGE,
		"5:20",// educated guess
		false,
		"A neighbor downlink signal must be this much stronger (in dB) than this downlink signal for handover to occur."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("GSM.Identity.BSIC.BCC","2",
		"",
		ConfigurationKey::CUSTOMERSITE,
		ConfigurationKey::VALRANGE,
		"0:7",
		false,
		"GSM basestation color code; lower 3 bits of the BSIC.  "
			"BCC values in a multi-BTS network should be assigned so that BTS units with overlapping coverage do not share a BCC.  "
			"This value will also select the training sequence used for all slots on this unit."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("GSM.Identity.BSIC.NCC","0",
		"",
		ConfigurationKey::CUSTOMERSITE,
		ConfigurationKey::VALRANGE,
		"0:7",
		false,
		"GSM network color code; upper 3 bits of the BSIC.  "
			"Assigned by your national regulator.  "
			"Must be distinct from NCCs of other GSM operators in your area."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("GSM.Identity.CI","10",
		"",
		ConfigurationKey::CUSTOMERSITE,
		ConfigurationKey::VALRANGE,
		"0:65535",
		false,
		"Cell ID, 16 bits.  "
			"Should be unique."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("GSM.Identity.LAC","1000",
		"",
		ConfigurationKey::CUSTOMERSITE,
		ConfigurationKey::VALRANGE,
		"0:65280",
		false,
		"Location area code, 16 bits, values 0xFFxx are reserved.  "
			"For multi-BTS networks, assign a unique LAC to each BTS unit.  "
			"(That is not the normal procedure in conventional GSM networks, but is the correct procedure in OpenBTS networks.)"
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("GSM.Identity.MCC","001",
		"",
		ConfigurationKey::CUSTOMERSITE,
		ConfigurationKey::STRING,
		"^[0-9]{3}$",
		false,
		"Mobile country code; must be three digits.  "
			"Defined in ITU-T E.212. 001 for test networks."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("GSM.Identity.MNC","01",
		"",
		ConfigurationKey::CUSTOMERSITE,
		ConfigurationKey::STRING,
		"^[0-9]{2,3}$",
		false,
		"Mobile network code, two or three digits.  "
			"Assigned by your national regulator. "
			"01 for test networks."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("GSM.Identity.ShortName","Range",
		"",
		ConfigurationKey::CUSTOMERSITE,
		ConfigurationKey::STRING,
		"^[[:alnum:]]+$",
		false,
		"Network short name, displayed on some phones.  "
			"Optional but must be defined if you also want the network to send time-of-day."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("GSM.MS.Power.Damping","50",
		"?damping value",
		ConfigurationKey::CUSTOMERTUNE,
		ConfigurationKey::VALRANGE,
		"25:75",// educated guess
		false,
		"Damping value for MS power control loop."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("GSM.MS.Power.Max","33",
		"dBm",
		ConfigurationKey::CUSTOMERTUNE,
		ConfigurationKey::VALRANGE,
		"5:100",// educated guess
		false,
		"Maximum commanded MS power level in dBm."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("GSM.MS.Power.Min","5",
		"dBm",
		ConfigurationKey::CUSTOMERTUNE,
		ConfigurationKey::VALRANGE,
		"5:100",// educated guess
		false,
		"Minimum commanded MS power level in dBm."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("GSM.MS.TA.Damping","50",
		"?damping value",
		ConfigurationKey::CUSTOMERTUNE,
		ConfigurationKey::VALRANGE,
		"25:75",// educated guess
		false,
		"Damping value for timing advance control loop."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("GSM.MS.TA.Max","62",
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
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("GSM.MaxSpeechLatency","2",
		"20 millisecond frames",
		ConfigurationKey::CUSTOMERTUNE,
		ConfigurationKey::VALRANGE,
		"1:5",// educated guess
		false,
		"Maximum allowed speech buffering latency, in 20 millisecond frames.  "
			"If the jitter is larger than this delay, frames will be lost."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("GSM.Neighbors","",
		"",
		ConfigurationKey::CUSTOMERWARN,
		ConfigurationKey::MIPADDRESS_OPT,// audited
		"",
		false,
		"A list of IP addresses of neighbor BTSs available for handover.  "
			"By default, this feature is disabled.  "
			"To enable, specify a space-separated list of the BTS IP addresses, in IP dotted notation, eg: 1.2.3.4 5.6.7.8.  "
			"To disable again, execute \"unconfig GSM.Neighbors\"."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("GSM.Neighbors.NumToSend","8",
		"neighbors",
		ConfigurationKey::CUSTOMERTUNE,
		ConfigurationKey::VALRANGE,
		"1:10",// educated guess
		false,
		"Maximum number of neighbors to send to handset in a neighbor list."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("GSM.Ny1","5",
		"repeats",
		ConfigurationKey::CUSTOMERTUNE,
		ConfigurationKey::VALRANGE,
		"1:10",// educated guess
		true,
		"Maximum number of repeats of the Physical Information Message during handover procedure, GSM 04.08 11.1.3."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("GSM.RACH.MaxRetrans","1",
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
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("GSM.RACH.TxInteger","14",
		"",
		ConfigurationKey::CUSTOMERTUNE,
		ConfigurationKey::CHOICE,
		"0|3 slots,"
			"1|4 slots,"
			"2|5 slots,"
			"3|6 slots,"
			"4|7 slots,"
			"5|8 slots,"
			"6|9 slots,"
			"7|10 slots,"
			"8|11 slots,"
			"9|12 slots,"
			"10|14 slots,"
			"11|16 slots,"
			"12|20 slots,"
			"13|25 slots,"
			"14|32 slots,"
			"15|50 slots",
		false,
		"Parameter to spread RACH busts over time.  "
			"This is the raw parameter sent on the BCCH.  "
			"See GSM 04.08 10.5.2.29 for encoding."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("GSM.RRLP.ACCURACY","40",
		"",
		ConfigurationKey::CUSTOMERTUNE,
		ConfigurationKey::VALRANGE,
		"20:60",// educated guess
		false,
		"Requested accuracy of location request. "
			"K in r=10(1.1**K-1), where r is the accuracy in meters. "
			"See 3GPP 03.32, sect 6.2"
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("GSM.RRLP.ALMANAC.ASSIST.PRESENT","0",
		"",
		ConfigurationKey::CUSTOMER,
		ConfigurationKey::BOOLEAN,
		"",
		false,
		"Send almanac info to mobile"
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("GSM.RRLP.ALMANAC.REFRESH.TIME","24.0",
		"hours",
		ConfigurationKey::CUSTOMERTUNE,
		ConfigurationKey::VALRANGE,
		"18.0:30.0(0.1)",// educated guess
		false,
		"How often the almanac is refreshed, in hours"
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("GSM.RRLP.ALMANAC.URL","http://www.navcen.uscg.gov/?pageName=currentAlmanac&format=yuma",
		"",
		ConfigurationKey::CUSTOMERWARN,
		ConfigurationKey::STRING,
		"^(http|ftp)://[[:alnum:]_.-]",
		false,
		"URL of almanac source."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("GSM.RRLP.EPHEMERIS.ASSIST.COUNT","9",
		"satellites",
		ConfigurationKey::CUSTOMERTUNE,
		ConfigurationKey::VALRANGE,
		"6:12",// educated guess
		false,
		"number of satellites to include in navigation model"
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("GSM.RRLP.EPHEMERIS.REFRESH.TIME","1.0",
		"hours",
		ConfigurationKey::CUSTOMERTUNE,
		ConfigurationKey::VALRANGE,
		"0.5:1.5(0.1)",// educated guess
		false,
		"How often the ephemeris is refreshed, in hours."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("GSM.RRLP.EPHEMERIS.URL","ftp://ftp.trimble.com/pub/eph/CurRnxN.nav",
		"",
		ConfigurationKey::CUSTOMERWARN,
		ConfigurationKey::STRING,
		"^(http|ftp)://[[:alnum:]_.-]",
		false,
		"URL of ephemeris source."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("GSM.RRLP.RESPONSETIME","4",
		"",
		ConfigurationKey::CUSTOMERTUNE,
		ConfigurationKey::VALRANGE,
		"2:6",// educated guess
		false,
		"Mobile timeout. "
			"(OpenBTS timeout is 130 sec = max response time + 2.) N in 2**N. "
			"See 3GPP 04.31 sect A.2.2.1"
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("GSM.RRLP.SEED.ALTITUDE","0",
		"meters",
		ConfigurationKey::CUSTOMERSITE,
		ConfigurationKey::VALRANGE,
		"-420:8850(5)",
		false,
		"Seed altitude in meters wrt geoidal surface."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("GSM.RRLP.SEED.LATITUDE","37.777423",
		"degrees",
		ConfigurationKey::CUSTOMERSITE,
		ConfigurationKey::VALRANGE,
		"-90.000000:90.000000",
		false,
		"Seed latitude in degrees.  "
			"-90 (south pole) .. +90 (north pole)"
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("GSM.RRLP.SEED.LONGITUDE","-122.39807",
		"degrees",
		ConfigurationKey::CUSTOMERSITE,
		ConfigurationKey::VALRANGE,
		"-180.000000:180.000000",
		false,
		"Seed longitude in degrees.  "
			"-180 (west of greenwich) .. 180 (east)"
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("GSM.RRLP.SERVER.URL","",
		"",
		ConfigurationKey::CUSTOMERWARN,
		ConfigurationKey::STRING_OPT,// audited
		"^(http|ftp)://[[:alnum:]_.-]",
		false,
		"URL of RRLP server.  "
			"By default, this feature is disabled.  "
			"To enable, specify a server URL eg: http://localhost/cgi/rrlpserver.cgi.  "
			"To disable again, execute \"unconfig GSM.RRLP.SERVER.URL\"."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("GSM.Radio.ARFCNs","1",
		"ARFCNs",
		ConfigurationKey::CUSTOMERWARN,
		ConfigurationKey::VALRANGE,
		"1:10",// educated guess
		true,
		"The number of ARFCNs to use.  "
			"The ARFCN set will be C0, C0+2, C0+4, etc."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("GSM.Radio.Band","900",
		"",
		ConfigurationKey::CUSTOMERWARN,
		ConfigurationKey::CHOICE,
		"850|GSM850,"
			"900|PGSM900,"
			"1800|DCS1800,"
			"1900|PCS1900",
		true,
		"The GSM operating band.  "
			"Valid values are 850 (GSM850), 900 (PGSM900), 1800 (DCS1800) and 1900 (PCS1900).  "
			"For non-multiband units, this value is dictated by the hardware and should not be changed."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("GSM.Radio.C0","51",
		"",
		ConfigurationKey::CUSTOMERSITE,
		ConfigurationKey::CHOICE,
		ConfigurationKey::getARFCNsString(),
		true,
		"The C0 ARFCN.  "
			"Also the base ARFCN for a multi-ARFCN configuration."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("GSM.Radio.MaxExpectedDelaySpread","4",
		"symbol periods",
		ConfigurationKey::CUSTOMERTUNE,
		ConfigurationKey::VALRANGE,
		"1:4",
		false,
		"Expected worst-case delay spread in symbol periods, roughly 3.7 us or 1.1 km per unit.  "
			"This parameter is dependent on the terrain type in the installation area.  "
			"Typical values are 1 for open terrain and small coverage areas.  "
			"For large coverage areas, a value of 4 is strongly recommended.  "
			"This parameter has a large effect on computational requirements of the software radio; values greater than 4 should be avoided."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("GSM.Radio.NeedBSIC","0",
		"",
		ConfigurationKey::FACTORY,
		ConfigurationKey::BOOLEAN,
		"",
		false,
		"Does the Radio type require the full BSIC"
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("GSM.Radio.PowerManager.MaxAttenDB","10",
		"dB",
		ConfigurationKey::CUSTOMERTUNE,
		ConfigurationKey::VALRANGE,
		"0:80",// educated guess
		false,
		"Maximum transmitter attenuation level, in dB wrt full scale on the D/A output.  "
			"This sets the minimum power output level in the output power control loop."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("GSM.Radio.PowerManager.MinAttenDB","0",
		"dB",
		ConfigurationKey::CUSTOMERTUNE,
		ConfigurationKey::VALRANGE,
		"0:80",// educated guess
		false,
		"Minimum transmitter attenuation level, in dB wrt full scale on the D/A output.  "
			"This sets the maximum power output level in the output power control loop."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("GSM.Radio.PowerManager.NumSamples","10",
		"sample count",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::VALRANGE,
		"5:20",// educated guess
		false,
		"Number of samples averaged by the output power control loop."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("GSM.Radio.PowerManager.Period","6000",
		"milliseconds",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::VALRANGE,
		"4500:7500(100)",// educated guess
		false,
		"Power manager control loop master period, in milliseconds."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("GSM.Radio.PowerManager.SamplePeriod","2000",
		"milliseconds",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::VALRANGE,
		"1500:2500(100)",// educated guess
		false,
		"Sample period for the output power control loopm in milliseconds."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("GSM.Radio.PowerManager.TargetT3122","5000",
		"milliseconds",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::VALRANGE,
		"3750:6250(100)",// educated guess
		false,
		"Target value for T3122, the random access hold-off timer, for the power control loop."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("GSM.Radio.RSSITarget","-50",
		"dB",
		ConfigurationKey::CUSTOMERTUNE,
		ConfigurationKey::VALRANGE,
		"-75:-25",// educated guess
		false,
		"Target uplink RSSI for MS power control loop, in dB wrt to A/D full scale.  "
			"Should be 6-10 dB above the noise floor."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("GSM.Radio.RxGain","0",
		"dB",
		ConfigurationKey::FACTORY,
		ConfigurationKey::VALRANGE,
		"0:75",// educated guess
		true,
		"Receiver gain setting in dB.  "
			"Ideal value is dictated by the hardware; 47 dB for RAD1, less for USRPs  "
			"This database parameter is static but the receiver gain can be modified in real time with the CLI rxgain command."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("GSM.ShowCountry","0",
		"",
		ConfigurationKey::CUSTOMER,
		ConfigurationKey::BOOLEAN,
		"",
		false,
		"Tell the phone to show the country name based on the MCC."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("GSM.Timer.T3103","5000",
		"milliseconds",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::VALRANGE,
		"2500:7500(100)",// educated guess
		true,
		"Handover timeout in milliseconds, GSM 04.08 11.1.2.  "
			"This is the timeout for a handset to sieze a channel during handover."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("GSM.Timer.T3105","50",
		"milliseconds",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::VALRANGE,
		"25:75(5)",// educated guess
		true,
		"Milliseconds for handset to respond to physical information. "
			"GSM 04.08 11.1.2."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("GSM.Timer.T3113","10000",
		"milliseconds",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::VALRANGE,
		"5000:15000(500)",// educated guess
		false,
		"Paging timer T3113 in milliseconds.  "
			"This is the timeout for a handset to respond to a paging request.  "
			"This should usually be the same as SIP.Timer.B in your VoIP network."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("GSM.Timer.T3122Max","255000",
		"milliseconds",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::VALRANGE,
		"127500:382500(1000)",// educated guess
		false,
		"Maximum allowed value for T3122, the RACH holdoff timer, in milliseconds."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("GSM.Timer.T3122Min","2000",
		"milliseconds",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::VALRANGE,
		"1000:3000(100)",// educated guess
		false,
		"Minimum allowed value for T3122, the RACH holdoff timer, in milliseconds."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("GSM.Timer.T3212","30",
		"minutes",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::VALRANGE,
		"0:1530(6)",
		false,
		"Registration timer T3212 period in minutes.  "
			"Should be a factor of 6.  "
			"Set to 0 to disable periodic registration.  "
			"Should be smaller than SIP registration period."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("Peering.Neighbor.RefreshAge","60000",
		"milliseconds",
		ConfigurationKey::CUSTOMERTUNE,
		ConfigurationKey::VALRANGE,
		"30000:90000(1000)",// educated guess
		false,
		"Milliseconds before refreshing parameters from a neighbor."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("Peering.NeighborTable.Path","/var/run/NeighborTable.db",
		"",
		ConfigurationKey::CUSTOMERWARN,
		ConfigurationKey::FILEPATH,
		"",
		true,
		"File path for neighbor information database."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("Peering.Port","16001",
		"",
		ConfigurationKey::CUSTOMERWARN,
		ConfigurationKey::PORT,
		"",
		true,
		"The UDP port used by the peer interface for handover."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("Peering.ResendCount","5",
		"attempts",
		ConfigurationKey::CUSTOMERTUNE,
		ConfigurationKey::VALRANGE,
		"3:8",// educated guess
		false,
		"Number of tries to send message over the peer interface before giving up"
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("Peering.ResendTimeout","100",
		"milliseconds",
		ConfigurationKey::CUSTOMERTUNE,
		ConfigurationKey::VALRANGE,
		"50:100(10)",// educated guess
		false,
		"Milliseconds before resending a message on the peer interface"
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("RTP.Range","98",
		"ports",
		ConfigurationKey::CUSTOMERTUNE,
		ConfigurationKey::VALRANGE,
		"25:200",// educated guess
		true,
		"Range of RTP port pool.  "
			"Pool is RTP.Start to RTP.Range-1."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("RTP.Start","16484",
		"",
		ConfigurationKey::CUSTOMERTUNE,
		ConfigurationKey::PORT,
		"",
		true,
		"Base of RTP port pool.  "
			"Pool is RTP.Start to RTP.Range-1."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("SGSN.Debug","0",
		"",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::BOOLEAN,
		"",
		false,
		"Add layer-3 messages to the GGSN.Logfile, if any."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("SGSN.Timer.ImplicitDetach","3480",
		"seconds",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::VALRANGE,
		"2000:4000(10)",// educated guess
		false,
		"3GPP 24.008 11.2.2.  "
			"GPRS attached MS is implicitly detached in seconds.  "
			"Should be at least 240 seconds greater than SGSN.Timer.RAUpdate.");
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("SGSN.Timer.MS.Idle","600",
		"?seconds",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::VALRANGE,
		"300:900(10)",// educated guess
		false,
		"How long an MS is idle before the SGSN forgets TLLI specific information."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("SGSN.Timer.RAUpdate","3240",
		"seconds",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::VALRANGE,
		"0:11160(2)",   // This is the allowed range of times expressed by the dopey GPRS Timer IE.  Max is 31 deci-hours.
		false,
		"Also known as T3312, 3GPP 24.008 4.7.2.2.  "
			"How often the MS reports into the SGSN when it is idle, in seconds.  "
			"Setting to 0 or >12000 deactivates entirely, i.e., sets the timer to effective infinity.  "
			"Note: to prevent GPRS Routing Area Updates you must set both this and GSM.Timer.T3212 to 0. "

	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("SGSN.Timer.Ready","44",
		"seconds",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::VALRANGE,
		"30:90",// educated guess
		false,
		"Also known as T3314, 3GPP 24.008 4.7.2.1.  "
			"Inactivity period required before MS may perform another routing area or cell update, in seconds."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("SIP.DTMF.RFC2833","1",
		"",
		ConfigurationKey::CUSTOMERWARN,
		ConfigurationKey::BOOLEAN,
		"",
		false,
		"Use RFC-2833 (RTP event signalling) for in-call DTMF."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("SIP.DTMF.RFC2833.PayloadType","101",
		"",
		ConfigurationKey::CUSTOMERWARN,
		ConfigurationKey::VALRANGE,
		"96:127",
		false,
		"Payload type to use for RFC-2833 telephone event packets."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("SIP.DTMF.RFC2967","0",
		"",
		ConfigurationKey::CUSTOMERWARN,
		ConfigurationKey::BOOLEAN,
		"",
		false,
		"Use RFC-2967 (SIP INFO method) for in-call DTMF."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("SIP.Local.IP","127.0.0.1",
		"",
		ConfigurationKey::CUSTOMERWARN,
		ConfigurationKey::IPADDRESS,
		"",
		true,
		"IP address of the OpenBTS machine as seen by its proxies.  "
			"If these are all local, this can be localhost."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("SIP.Local.Port","5062",
		"",
		ConfigurationKey::CUSTOMERWARN,
		ConfigurationKey::PORT,
		"",
		true,
		"IP port that OpenBTS uses for its SIP interface."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("SIP.MaxForwards","70",
		"referrals",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::VALRANGE,
		"1:100",
		false,
		"Maximum allowed number of referrals."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("SIP.Proxy.Registration","127.0.0.1:5064",
		"",
		ConfigurationKey::CUSTOMERWARN,
		ConfigurationKey::IPANDPORT,
		"",
		false,
		"The IP host and port of the proxy to be used for registration and authentication.  "
			"This should normally be the subscriber registry SIP interface, not Asterisk."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("SIP.Proxy.SMS","127.0.0.1:5063",
		"",
		ConfigurationKey::CUSTOMERWARN,
		ConfigurationKey::IPANDPORT,
		"",
		false,
		"The IP host and port of the proxy to be used for text messaging.  "
			"This is smqueue, for example."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("SIP.Proxy.Speech","127.0.0.1:5060",
		"",
		ConfigurationKey::CUSTOMERWARN,
		ConfigurationKey::IPANDPORT,
		"",
		false,
		"The IP host and port of the proxy to be used for normal speech calls.  "
			"This is Asterisk, for example."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("SIP.RegistrationPeriod","90",
		"minutes",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::VALRANGE,
		"6:2298",// educated guess
		false,
		"Registration period in minutes for MS SIP users.  "
			"Should be longer than GSM T3212."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("SIP.RFC3428.NoTrying","0",
		"",
		ConfigurationKey::CUSTOMERWARN,
		ConfigurationKey::BOOLEAN,
		"",
		false,
		"Send 100 Trying response to SIP MESSAGE, even though that violates RFC-3428."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("SIP.SMSC","smsc",
		"",
		ConfigurationKey::CUSTOMERWARN,
		ConfigurationKey::CHOICE_OPT,// audited
		"smsc",
		false,
		"The SMSC handler in smqueue.  "
			"This is the entity that handles full 3GPP MIME-encapsulted TPDUs.  "
			"If not defined, use direct numeric addressing.  "
			"The value should be disabled with \"unconfig SIP.SMSC\" if SMS.MIMEType is \"text/plain\" or set to \"smsc\" if SMS.MIMEType is \"application/vnd.3gpp\"."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("SIP.Timer.A","2000",
		"milliseconds",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::VALRANGE,
		"1500:2500(100)",// educated guess
		false,
		"SIP timer A, the INVITE retry period, RFC-3261 Section 17.1.1.2, in milliseconds."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("SIP.Timer.B","10000",
		"milliseconds",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::VALRANGE,
		"5000:15000(100)",// educated guess
		false,
		"INVITE transaction timeout in milliseconds.  "
			"This value should usually match GSM.Timer.T3113."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("SIP.Timer.E","500",
		"milliseconds",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::VALRANGE,
		"250:750(10)",// educated guess
		false,
		"Non-INVITE initial request retransmit period in milliseconds."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("SIP.Timer.F","5000",
		"milliseconds",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::VALRANGE,
		"2500:7500(100)",// educated guess
		false,
		"Non-INVITE initial request timeout in milliseconds."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("SIP.Timer.H","5000",
		"milliseconds",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::VALRANGE,
		"2500:7500(100)",// educated guess
		false,
		"ACK timeout period in milliseconds."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("SMS.FakeSrcSMSC","0000",
		"",
		ConfigurationKey::CUSTOMERWARN,
		ConfigurationKey::STRING,
		"^[0-9]+$",
		false,
		"Use this to fill in L4 SMSC address in SMS delivery."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("SMS.MIMEType","application/vnd.3gpp.sms",
		"",
		ConfigurationKey::CUSTOMERWARN,
		ConfigurationKey::CHOICE,
		"application/vnd.3gpp.sms,"
			"text/plain",
		false,
		"This is the MIME Type that OpenBTS will use for RFC-3428 SIP MESSAGE payloads.  "
			"Valid values are \"application/vnd.3gpp.sms\" and \"text/plain\"."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("SubscriberRegistry.A3A8","/OpenBTS/comp128",
		"",
		ConfigurationKey::CUSTOMERWARN,
		ConfigurationKey::FILEPATH,
		"",
		false,
		"Path to the program that implements the A3/A8 algorithm."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("SubscriberRegistry.db","/var/lib/asterisk/sqlite3dir/sqlite3.db",
		"",
		ConfigurationKey::CUSTOMERWARN,
		ConfigurationKey::FILEPATH,
		"",
		false,
		"The location of the sqlite3 database holding the subscriber registry."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("SubscriberRegistry.Manager.Title","Subscriber Registry",
		"",
		ConfigurationKey::CUSTOMER,
		ConfigurationKey::STRING,
		"^[[:print:]]+$",
		false,
		"Title text to be displayed on the subscriber registry manager."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("SubscriberRegistry.Manager.VisibleColumns","name username type context host",
		"",
		ConfigurationKey::CUSTOMERTUNE,
		ConfigurationKey::STRING,
		"^(name){0,1} (username){0,1} (type){0,1} (context){0,1} (host){0,1}$",
		false,
		"A space separated list of columns to display in the subscriber registry manager."
	);

	tmp = new ConfigurationKey("SubscriberRegistry.Port","5064",
		"",
		ConfigurationKey::CUSTOMERWARN,
		ConfigurationKey::PORT,
		"",
		false,
		"Port used by the SIP Authentication Server. NOTE: In some older releases (pre-2.8.1) this is called SIP.myPort."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("SubscriberRegistry.UpstreamServer","",
		"",
		ConfigurationKey::CUSTOMERWARN,
		ConfigurationKey::STRING_OPT,// audited
		"",
		false,
		"URL of the subscriber registry HTTP interface on the upstream server.  "
			"By default, this feature is disabled.  "
			"To enable, specify a server URL eg: http://localhost/cgi/subreg.cgi.  "
			"To disable again, execute \"unconfig SubscriberRegistry.UpstreamServer\"."
			);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("TRX.IP","127.0.0.1",
		"",
		ConfigurationKey::CUSTOMERWARN,
		ConfigurationKey::IPADDRESS,
		"",
		true,
		"IP address of the transceiver application."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("TRX.MinimumRxRSSI","-63",
		"dB",
		ConfigurationKey::FACTORY,
		ConfigurationKey::VALRANGE,
		"-90:90",// educated guess
		false,
		"Bursts received at the physical layer below this threshold are automatically ignored.  "
			"Values in dB.  "
			"Set at the factory.  "
			"Do not adjust without proper calibration."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("TRX.Port","5700",
		"",
		ConfigurationKey::FACTORY,
		ConfigurationKey::PORT,
		"",
		true,
		"IP port of the transceiver application."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("TRX.RadioFrequencyOffset","128",
		"~170Hz steps",
		ConfigurationKey::FACTORY,
		ConfigurationKey::VALRANGE,
		"64:192",
		true,
		"Fine-tuning adjustment for the transceiver master clock.  "
			"Roughly 170 Hz/step.  "
			"Set at the factory.  "
			"Do not adjust without proper calibration."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("TRX.Timeout.Clock","10",
		"seconds",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::VALRANGE,
		"5:15",// educated guess
		false,
		"How long to wait during a read operation from the transceiver before giving up."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	// unused?
	tmp = new ConfigurationKey("TRX.Timeout.Start","2",
		"seconds",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::VALRANGE,
		"1:3",// educated guess
		false,
		"How long to wait during system startup before checking to see if the transceiver can be reached."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("TRX.TxAttenOffset","0",
		"dB of attenuation",
		ConfigurationKey::FACTORY,
		ConfigurationKey::VALRANGE,
		"0:100",// educated guess
		true,
		"Hardware-specific gain adjustment for transmitter, matched to the power amplifier, expessed as an attenuationi in dB.  "
			"Set at the factory.  "
			"Do not adjust without proper calibration."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	//kurtis
	tmp = new ConfigurationKey("TRX.Args","",
		"",
		ConfigurationKey::CUSTOMER,
		ConfigurationKey::STRING,
		"",
		false,
		"Extra arguments for the Transceiver"
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("Test.GSM.SimulatedFER.Downlink","0",
		"probability in %",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::VALRANGE,
		"0:100(5)",// educated guess
		true,
		"Probability (0-100) of dropping any downlink frame to test robustness."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("Test.GSM.SimulatedFER.Uplink","0",
		"probability in %",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::VALRANGE,
		"0:100(5)",// educated guess
		true,
		"Probability (0-100) of dropping any uplink frame to test robustness."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("Test.GSM.UplinkFuzzingRate","0",
		"probability in %",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::VALRANGE,
		"0:100(5)",// educated guess
		true,
		"Probability (0-100) of flipping a bit in any uplink frame to test robustness."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("Test.SIP.SimulatedPacketLoss","0",
		"probability in %",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::VALRANGE,
		"0:100(5)",// educated guess
		true,
		"Probability (0-100) of dropping any inbound or outbound SIP packet to test robustness."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	return map;
}
