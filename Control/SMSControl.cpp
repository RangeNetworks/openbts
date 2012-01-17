/**@file SMS Control (L3), GSM 03.40, 04.11. */
/*
* Copyright 2008, 2009, 2011 Free Software Foundation, Inc.
* Copyright 2010 Kestrel Signal Processing, Inc.
* Copyright 2011 Range Networks, Inc.
*
*
* This use of this software may be subject to additional restrictions.
* See the LEGAL file in the main directory for details.

	This program is free software: you can redistribute it and/or modify
	it under the terms of the GNU Affero General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU Affero General Public License for more details.

	You should have received a copy of the GNU Affero General Public License
	along with this program.  If not, see <http://www.gnu.org/licenses/>.

*/


/*
	Abbreviations:
	MOSMS -- Mobile Originated Short Message Service
	MTSMS -- Mobile Terminated Short Message Service

	Verbs:
	"send" -- Transfer to the network.
	"receive" -- Transfer from the network.
	"submit" -- Transfer from the MS.
	"deliver" -- Transfer to the MS.

	MOSMS: The MS "submits" a message that OpenBTS then "sends" to the network.
	MTSMS: OpenBTS "receives" a message from the network that it then "delivers" to the MS.

*/


#include <stdio.h>
#include <sstream>
#include <GSMLogicalChannel.h>
#include <GSML3MMMessages.h>
#include "SMSControl.h"
#include "ControlCommon.h"
#include "TransactionTable.h"
#include "RRLPServer.h"
#include <Regexp.h>



using namespace std;
using namespace Control;

#include "SMSMessages.h"
using namespace SMS;

#include "SIPInterface.h"
#include "SIPUtility.h"
#include "SIPMessage.h"
#include "SIPEngine.h"
using namespace SIP;

#include <Logger.h>
#undef WARNING

/**
	Read an L3Frame from SAP3.
	Throw exception on failure.  Will NOT return a NULL pointer.
*/
GSM::L3Frame* getFrameSMS(GSM::LogicalChannel *LCH, GSM::Primitive primitive=GSM::DATA)
{
	GSM::L3Frame *retVal = LCH->recv(LCH->N200()*LCH->T200(),3);
	if (!retVal) {
		LOG(NOTICE) << "channel read time out on " << *LCH << " SAP3";
		throw ChannelReadTimeout();
	}
	LOG(DEBUG) << "getFrameSMS on " << *LCH << " in frame " << *retVal;
	if (retVal->primitive() != primitive) {
		LOG(NOTICE) << "unexpected primitive on " << *LCH << ", expecting " << primitive << ", got " << *retVal;
		throw UnexpectedPrimitive();
	}
	if ((retVal->primitive() == GSM::DATA) && (retVal->PD() != GSM::L3SMSPD)) {
		LOG(NOTICE) << "unexpected (non-SMS) protocol on " << *LCH << " in frame " << *retVal;
		throw UnexpectedMessage();
	}
	return retVal;
}


bool sendSIP(TransactionEntry *transaction, const char* address, const char* body, const char* contentType)
{
	// Steps:
	// 1 -- Complete transaction record.
	// 2 -- Send TL-SUBMIT to the server.
	// 3 -- Wait for response or timeout.
	// 4 -- Return true for OK or ACCEPTED, false otherwise.

	// Step 1 -- Complete the transaction record.
	// Form the TLAddress into a CalledPartyNumber for the transaction.
	GSM::L3CalledPartyBCDNumber calledParty(address);
	// Attach calledParty and message body to the transaction.
	transaction->called(calledParty);
	transaction->message(body,strlen(body));

	// Step 2 -- Send the message to the server.
	transaction->MOSMSSendMESSAGE(address,gConfig.getStr("SIP.Local.IP").c_str(),contentType);

	// Step 3 -- Wait for OK or ACCEPTED.
	SIPState state = transaction->MOSMSWaitForSubmit();

	// Step 4 -- Done
	return state==SIP::Cleared;
}

/**
	Process the RPDU.
	@param mobileID The sender's IMSI.
	@param RPDU The RPDU to process.
	@return true if successful.
*/
bool handleRPDU(TransactionEntry *transaction, const RLFrame& RPDU)
{
	LOG(DEBUG) << "SMS: handleRPDU MTI=" << RPDU.MTI();
	switch ((RPMessage::MessageType)RPDU.MTI()) {
		case RPMessage::Data: {
			string contentType = gConfig.getStr("SMS.MIMEType");
			ostringstream body;

			if (contentType == "text/plain") {
				// TODO: Clean this mess up!
				RPData data;
				data.parse(RPDU);
				TLSubmit submit;
				submit.parse(data.TPDU());
				
				body << submit.UD().decode();
			} else if (contentType == "application/vnd.3gpp.sms") {
				RPDU.hex(body);
			} else {
				LOG(ALERT) << "\"" << contentType << "\" is not a valid SMS payload type";
			}
			const char* address = NULL;
			if (gConfig.defines("SIP.SMSC")) address = gConfig.getStr("SIP.SMSC").c_str();

			/* The SMSC is not defined, we are using an older version */
			if (address == NULL) {
				RPData data;
				data.parse(RPDU);
				TLSubmit submit;
				submit.parse(data.TPDU());

				address = submit.DA().digits();
			}
			return sendSIP(transaction, address, body.str().data(),contentType.c_str());
		}
		case RPMessage::Ack:
		case RPMessage::SMMA:
			return true;
		case RPMessage::Error:
		default:
			return false;
	}
}




void Control::MOSMSController(const GSM::L3CMServiceRequest *req, GSM::LogicalChannel *LCH)
{
	assert(req);
	assert(req->serviceType().type() == GSM::L3CMServiceType::ShortMessage);
	assert(LCH);
	assert(LCH->type() != GSM::SACCHType);

	LOG(INFO) << "MOSMS, req " << *req;

	// If we got a TMSI, find the IMSI.
	// Note that this is a copy, not a reference.
	GSM::L3MobileIdentity mobileID = req->mobileID();
	resolveIMSI(mobileID,LCH);

	// Create a transaction record.
	TransactionEntry *transaction = new TransactionEntry(gConfig.getStr("SIP.Proxy.SMS").c_str(),mobileID,LCH);
	gTransactionTable.add(transaction);
	LOG(DEBUG) << "MOSMS: transaction: " << *transaction;

	// See GSM 04.11 Arrow Diagram A5 for the transaction
	// Step 1	MS->Network	CP-DATA containing RP-DATA
	// Step 2	Network->MS	CP-ACK
	// Step 3	Network->MS	CP-DATA containing RP-ACK
	// Step 4	MS->Network	CP-ACK

	// LAPDm operation, from GSM 04.11, Annex F:
	// """
	// Case A: Mobile originating short message transfer, no parallel call:
	// The mobile station side will initiate SAPI 3 establishment by a SABM command
	// on the DCCH after the cipher mode has been set. If no hand over occurs, the
	// SAPI 3 link will stay up until the last CP-ACK is received by the MSC, and
	// the clearing procedure is invoked.
	// """

	// FIXME: check provisioning

	// Let the phone know we're going ahead with the transaction.
	LOG(INFO) << "sending CMServiceAccept";
	LCH->send(GSM::L3CMServiceAccept());
	// Wait for SAP3 to connect.
	// The first read on SAP3 is the ESTABLISH primitive.
	delete getFrameSMS(LCH,GSM::ESTABLISH);

	// Step 1
	// Now get the first message.
	// Should be CP-DATA, containing RP-DATA.
	GSM::L3Frame *CM = getFrameSMS(LCH);
	LOG(DEBUG) << "data from MS " << *CM;
	if (CM->MTI()!=CPMessage::DATA) {
		LOG(NOTICE) << "unexpected SMS CP message with TI=" << CM->MTI();
		throw UnexpectedMessage();
	}
	unsigned L3TI = CM->TI() | 0x08;
	transaction->L3TI(L3TI);

	// Step 2
	// Respond with CP-ACK.
	// This just means that we got the message.
	LOG(INFO) << "sending CPAck";
	LCH->send(CPAck(L3TI),3);

	// Parse the message in CM and process RP part.
	// This is where we actually parse the message and send it out.
	// FIXME -- We need to set the message ref correctly,
	// even if the parsing fails.
	// The compiler gives a warning here.  Let it.  It will remind someone to fix it.
	unsigned ref;
	bool success = false;
	try {
		CPData data;
		data.parse(*CM);
		delete CM;
		LOG(INFO) << "CPData " << data;
		// Transfer out the RPDU -> TPDU -> delivery.
		ref = data.RPDU().reference();
		// This handler invokes higher-layer parsers, too.
		success = handleRPDU(transaction,data.RPDU());
	}
	catch (SMSReadError) {
		LOG(WARNING) << "SMS parsing failed (above L3)";
		// Cause 95, "semantically incorrect message".
		LCH->send(CPData(L3TI,RPError(95,ref)),3);
		throw UnexpectedMessage();
	}
	catch (GSM::L3ReadError) {
		LOG(WARNING) << "SMS parsing failed (in L3)";
		throw UnsupportedMessage();
	}

	// Step 3
	// Send CP-DATA containing RP-ACK and message reference.
	if (success) {
		LOG(INFO) << "sending RPAck in CPData";
		LCH->send(CPData(L3TI,RPAck(ref)),3);
	} else {
		LOG(INFO) << "sending RPError in CPData";
		// Cause 127 is "internetworking error, unspecified".
		// See GSM 04.11 Table 8.4.
		LCH->send(CPData(L3TI,RPError(127,ref)),3);
	}

	// Step 4
	// Get CP-ACK from the MS.
	CM = getFrameSMS(LCH);
	if (CM->MTI()!=CPMessage::ACK) {
		LOG(NOTICE) << "unexpected SMS CP message with TI=" << CM->MTI();
		throw UnexpectedMessage();
	}
	LOG(DEBUG) << "ack from MS: " << *CM;
	CPAck ack;
	ack.parse(*CM);
	LOG(INFO) << "CPAck " << ack;

	/* MOSMS RLLP request */
	if (gConfig.defines("Control.SMS.QueryRRLP")) {
		// Query for RRLP
		if (!sendRRLP(mobileID, LCH)) {
			LOG(INFO) << "RRLP request failed";
		}
	}

	// Done.
	LCH->send(GSM::L3ChannelRelease());
	gTransactionTable.remove(transaction);
	LOG(INFO) << "closing the Um channel";
}




bool Control::deliverSMSToMS(const char *callingPartyDigits, const char* message, const char* contentType, unsigned L3TI, GSM::LogicalChannel *LCH)
{
	if (!LCH->multiframeMode(3)) {
		// Start ABM in SAP3.
		LCH->send(GSM::ESTABLISH,3);
		// Wait for SAP3 ABM to connect.
		// The next read on SAP3 should the ESTABLISH primitive.
		// This won't return NULL.  It will throw an exception if it fails.
		delete getFrameSMS(LCH,GSM::ESTABLISH);
	}

#if 0
	// HACK -- Check for "Easter Eggs"
	// TL-PID
	unsigned TLPID=0;
	if (strncmp(message,"#!TLPID",7)==0) sscanf(message,"#!TLPID%d",&TLPID);

	// Step 1
	// Send the first message.
	// CP-DATA, containing RP-DATA.
	unsigned reference = random() % 255;
	CPData deliver(L3TI,
		RPData(reference,
			RPAddress(gConfig.getStr("SMS.FakeSrcSMSC").c_str()),
			TLDeliver(callingPartyDigits,message,TLPID)));
#else
	// TODO: Read MIME Type from smqueue!!
	unsigned reference = random() % 255;
	RPData rp_data;

	if (strncmp(contentType,"text/plain",10)==0) {
		rp_data = RPData(reference,
			RPAddress(gConfig.getStr("SMS.FakeSrcSMSC").c_str()),
			TLDeliver(callingPartyDigits,message,0));
	} else if (strncmp(contentType,"application/vnd.3gpp.sms",24)==0) {
		BitVector RPDUbits(strlen(message)*4);
		if (!RPDUbits.unhex(message)) {
			LOG(WARNING) << "Hex string parsing failed (in incoming SIP MESSAGE)";
			throw UnexpectedMessage();
		}

		try {
			RLFrame RPDU(RPDUbits);
			LOG(DEBUG) << "SMS RPDU: " << RPDU;

			rp_data.parse(RPDU);
			LOG(DEBUG) << "SMS RP-DATA " << rp_data;
		}
		catch (SMSReadError) {
			LOG(WARNING) << "SMS parsing failed (above L3)";
			// Cause 95, "semantically incorrect message".
			LCH->send(CPData(L3TI,RPError(95,reference)),3);
			throw UnexpectedMessage();
		}
		catch (GSM::L3ReadError) {
			LOG(WARNING) << "SMS parsing failed (in L3)";
			// TODO:: send error back to the phone
			throw UnsupportedMessage();
		}
	} else {
		LOG(WARNING) << "Unsupported content type (in incoming SIP MESSAGE) -- type: " << contentType;
		throw UnexpectedMessage();
	}

	CPData deliver(L3TI,rp_data);

#endif

	// Start ABM in SAP3.
	//LCH->send(GSM::ESTABLISH,3);
	// Wait for SAP3 ABM to connect.
	// The next read on SAP3 should the ESTABLISH primitive.
	// This won't return NULL.  It will throw an exception if it fails.
	//delete getFrameSMS(LCH,GSM::ESTABLISH);

	LOG(INFO) << "sending " << deliver;
	LCH->send(deliver,3);

	// Step 2
	// Get the CP-ACK.
	// FIXME -- Check TI.
	LOG(DEBUG) << "MTSMS: waiting for CP-ACK";
	GSM::L3Frame *CM = getFrameSMS(LCH);
	LOG(DEBUG) << "MTSMS: ack from MS " << *CM;
	if (CM->MTI()!=CPMessage::ACK) {
		LOG(WARNING) << "MS rejected our RP-DATA with CP message with TI=" << CM->MTI();
		throw UnexpectedMessage();
	}

	// Step 3
	// Get CP-DATA containing RP-ACK and message reference.
	LOG(DEBUG) << "MTSMS: waiting for RP-ACK";
	CM = getFrameSMS(LCH);
	LOG(DEBUG) << "MTSMS: data from MS " << *CM;
	if (CM->MTI()!=CPMessage::DATA) {
		LOG(NOTICE) << "Unexpected SMS CP message with TI=" << CM->MTI();
		throw UnexpectedMessage();
	}

	// FIXME -- Check L3 TI.

	// Parse to check for RP-ACK.
	CPData data;
	try {
		data.parse(*CM);
		delete CM;
		LOG(DEBUG) << "CPData " << data;
	}
	catch (SMSReadError) {
		LOG(WARNING) << "SMS parsing failed (above L3)";
		// Cause 95, "semantically incorrect message".
		LCH->send(CPError(L3TI,95),3);
		throw UnexpectedMessage();
	}
	catch (GSM::L3ReadError) {
		LOG(WARNING) << "SMS parsing failed (in L3)";
		throw UnsupportedMessage();
	}

	// FIXME -- Check SMS reference.

	bool success = true;
	if (data.RPDU().MTI()!=RPMessage::Ack) {
		LOG(WARNING) << "unexpected RPDU " << data.RPDU();
		success = false;
	}

	// Step 4
	// Send CP-ACK to the MS.
	LOG(INFO) << "MTSMS: sending CPAck";
	LCH->send(CPAck(L3TI),3);
	return success;
}




void Control::MTSMSController(TransactionEntry *transaction, GSM::LogicalChannel *LCH)
{
	assert(LCH);
	assert(transaction);

	// See GSM 04.11 Arrow Diagram A5 for the transaction
	// Step 1	Network->MS	CP-DATA containing RP-DATA
	// Step 2	MS->Network	CP-ACK
	// Step 3	MS->Network	CP-DATA containing RP-ACK
	// Step 4	Network->MS	CP-ACK

	// LAPDm operation, from GSM 04.11, Annex F:
	// """
	// Case B: Mobile terminating short message transfer, no parallel call:
	// The network side, i.e. the BSS will initiate SAPI3 establishment by a
	// SABM command on the DCCH when the first CP-Data message is received
	// from the MSC. If no hand over occurs, the link will stay up until the
	// MSC has given the last CP-ack and invokes the clearing procedure. 
	// """


	// Attach the channel to the transaction and update the state.
	LOG(DEBUG) << "transaction: "<< *transaction;
	transaction->channel(LCH);
	transaction->GSMState(GSM::SMSDelivering);
	LOG(INFO) << "transaction: "<< *transaction;

	/* MTSMS RLLP request */
	if (gConfig.defines("Control.SMS.QueryRRLP")) {
		// Query for RRLP
		if(!sendRRLP(transaction->subscriber(), LCH)){
	  		LOG(INFO) << "RRLP request failed";
		}
	}

	bool success = deliverSMSToMS(transaction->calling().digits(),transaction->message(),
								transaction->messageType(),transaction->L3TI(),LCH);

	// Close the Dm channel?
	if (LCH->type()!=GSM::SACCHType) {
		LCH->send(GSM::L3ChannelRelease());
		LOG(INFO) << "closing the Um channel";
	}

	// Ack in SIP domain.
	if (success) transaction->MTSMSSendOK();

	// Done.
	gTransactionTable.remove(transaction);
}




void Control::InCallMOSMSStarter(TransactionEntry *parallelCall)
{
	GSM::LogicalChannel *hostChan = parallelCall->channel();
	assert(hostChan);
	GSM::LogicalChannel *SACCH = hostChan->SACCH();
	assert(SACCH);
	
	// Create a partial transaction record.
	TransactionEntry *newTransaction = new TransactionEntry(
		gConfig.getStr("SIP.Proxy.SMS").c_str(),
		parallelCall->subscriber(),
		SACCH);
	gTransactionTable.add(newTransaction);
}



void Control::InCallMOSMSController(const CPData *cpData, TransactionEntry* transaction, GSM::SACCHLogicalChannel *LCH)
{
	LOG(INFO) << *cpData;

	// See GSM 04.11 Arrow Diagram A5 for the transaction
	// Step 1	MS->Network	CP-DATA containing RP-DATA
	// Step 2	Network->MS	CP-ACK
	// Step 3	Network->MS	CP-DATA containing RP-ACK
	// Step 4	MS->Network	CP-ACK

	// LAPDm operation, from GSM 04.11, Annex F:
	// """
	// Case C: Mobile originating short message transfer, parallel call. 
	// The mobile station will send a SABM command on the SACCH when a CM_SERV_ACC
	// message has been received from the network, allowing the short message
	// transfer to start. If no hand over occurs the link will stay up until the
	// MSC orders a explicit release, or the clearing procedure is invoked. If the
	// parallel call is cleared before the short message transfer is finalized, the
	// MSC will delay the clearing procedure toward the BSS, i.e. the channel
	// release procedure is delayed. 
	// """

	// Since there's a parallel call, we will assume correct provisioning.
	// And we know that CM and SABM are established.

	// Step 1 already happened in the SACCH service loop.
	// Just get the L3 TI and set the high bit since it originated in the MS.
	unsigned L3TI = cpData->TI() | 0x08;
	transaction->L3TI(L3TI);

	// Step 2
	// Respond with CP-ACK.
	// This just means that we got the message.
	LOG(INFO) << "sending CPAck";
	LCH->send(CPAck(L3TI),3);

	// Parse the message in CM and process RP part.
	// This is where we actually parse the message and send it out.
	// FIXME -- We need to set the message ref correctly,
	// even if the parsing fails.
	// The compiler gives a warning here.  Let it.  It will remind someone to fix it.
	unsigned ref;
	bool success = false;
	try {
		CPData data;
		data.parse(*cpData);
		LOG(INFO) << "CPData " << data;
		// Transfer out the RPDU -> TPDU -> delivery.
		ref = data.RPDU().reference();
		// This handler invokes higher-layer parsers, too.
		success = handleRPDU(transaction,data.RPDU());
	}
	catch (SMSReadError) {
		LOG(WARNING) << "SMS parsing failed (above L3)";
		// Cause 95, "semantically incorrect message".
		LCH->send(CPData(L3TI,RPError(95,ref)),3);
		throw UnexpectedMessage(transaction->ID());
	}
	catch (GSM::L3ReadError) {
		LOG(WARNING) << "SMS parsing failed (in L3)";
		throw UnsupportedMessage(transaction->ID());
	}

	// Step 3
	// Send CP-DATA containing RP-ACK and message reference.
	if (success) {
		LOG(INFO) << "sending RPAck in CPData";
		LCH->send(CPData(L3TI,RPAck(ref)),3);
	} else {
		LOG(INFO) << "sending RPError in CPData";
		// Cause 127 is "internetworking error, unspecified".
		// See GSM 04.11 Table 8.4.
		LCH->send(CPData(L3TI,RPError(127,ref)),3);
	}

	// Step 4
	// Get CP-ACK from the MS.
	GSM::L3Frame* CM = getFrameSMS(LCH);
	if (CM->MTI()!=CPMessage::ACK) {
		LOG(NOTICE) << "unexpected SMS CP message with MTI=" << CM->MTI() << " " << *CM;
		throw UnexpectedMessage(transaction->ID());
	}
	LOG(DEBUG) << "ack from MS: " << *CM;
	CPAck ack;
	ack.parse(*CM);
	LOG(INFO) << "CPAck " << ack;

	/* I had a hell of a time testing this with my B100
	   I know it went off, that's all. If things fail, look
	   here -kurtis */

	/* MOSMS RLLP request */
	if (gConfig.defines("Control.SMS.QueryRRLP")) {
		// Query for RRLP
		if (!sendRRLP(transaction->subscriber(), LCH)) {
			LOG(INFO) << "RRLP request failed";
		}
	}

	gTransactionTable.remove(transaction);
}

// vim: ts=4 sw=4
