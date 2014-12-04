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

#define LOG_GROUP LogGroup::Control

#include "ControlCommon.h"
#include "L3StateMachine.h"
#include "L3TranEntry.h"
#include "L3MMLayer.h"
#include "L3SMSControl.h"
#include <GSMCommon.h>
//#include <GSMConfig.h>
#include <GSML3Message.h>
#include <GSMLogicalChannel.h>
#include <SMSMessages.h>
#include <Globals.h>
#include <CLI.h>


namespace Control {
using namespace GSM;
using namespace SMS;
using namespace SIP;

struct SMSCommon : public MachineBase {
	unsigned mRpduRef;
	SMSCommon(TranEntry *tran) : MachineBase(tran) {}
	void l3sendSms(const L3Message &msg);		// Send an SMS message to the correct place.
	//L3LogicalChannel *getSmsChannel() const;
	SAPI_t getSmsSap() const;
};


class MOSMSMachine : public SMSCommon
{
	enum State {	// These are the machineRunState states for our State Machine.
		stateStartUnused,	// unused.
		stateIdentResult
	};
	SmsState mSmsState;
	bool mIdentifyResult;
	MachineStatus machineRunState(int state, const GSM::L3Message *l3msg, const SIP::DialogMessage *sipmsg);
	bool handleRPDU(const RLFrame& RPDU);
	public:
	MOSMSMachine(TranEntry *tran) : SMSCommon(tran), mSmsState(MoSmsIdle) {}
	const char *debugName() const { return "MOSMSMachine"; }
	friend void startMOSMS(const GSM::L3MMMessage *l3msg, MMContext *mmchan);
};

class MTSMSMachine : public SMSCommon
{
	enum State {	// These are the machineRunState states for our State Machine.
		stateStart,
	};
	// Use machineRunState1 so we can get the ESTABLISH primitive:
	MachineStatus machineRunState1(int state, const L3Frame *frame, const L3Message *l3msg, const SIP::DialogMessage *sipmsg);
	bool handleRPDU(const RLFrame& RPDU);
	bool createRPData(RPData &rp_data);
	public:
	MTSMSMachine(TranEntry *tran) : SMSCommon(tran) {}
	const char *debugName() const { return "MTSMSMachine"; }
};


//L3LogicalChannel *SMSCommon::getSmsChannel() const
//{
//	if (channel()->isSDCCH()) {
//		return channel();		// Use main SDCCH.
//	} else {
//		assert(channel()->isTCHF());
//		return channel()->getSACCHL3();	// Use SACCH associated with TCH.
//	}
//}

SAPI_t SMSCommon::getSmsSap() const
{
	if (channel()->isSDCCH()) {
		return SAPI3;		// The SDCCH is faster than SACCH.
	} else {
		assert(channel()->isTCHF());
		return SAPI3Sacch; // Use SACCH associated with TCH.
	}
}

void SMSCommon::l3sendSms(const L3Message &msg)
{
	channel()->l3sendm(msg,GSM::L3_DATA,getSmsSap());
}


/**
	Process the incomming RPDU.
	@param mobileID The sender's IMSI.
	@param RPDU The RPDU to process.
	@return true if successful.
*/
bool MOSMSMachine::handleRPDU(const RLFrame& RPDU)
{
	LOG(DEBUG) << "SMS: handleRPDU MTI=" << RPDU.MTI();
	switch ((RPMessage::MessageType)RPDU.MTI()) {
		case RPMessage::Data: {
			string contentType = gConfig.getStr("SMS.MIMEType");
			ostringstream body;
			string toAddress = "";

			if (contentType == "text/plain") {
				RPData data;
				data.parse(RPDU);
				TLSubmit submit;
				submit.parse(data.TPDU());
				
				body << submit.UD().decode();	// (pat) TODO: efficiencize this.
				toAddress = string(submit.DA().digits());
			} else if (contentType == "application/vnd.3gpp.sms") {
				toAddress = "smsc";  // If encoded this is expected in destination address
				RPDU.hex(body);
			} else {
				LOG(ERR) << "\"" << contentType << "\" is not a valid SMS payload type";
			}
			// Steps:
			// 1 -- Complete transaction record.
			// 2 -- Send TL-SUBMIT to the server.
			// 3 -- Wait for response or timeout.
			// 4 -- Return true for OK or ACCEPTED, false otherwise.

			// Step 1 and 2 -- Complete the transaction record and send TL-SUMBIT to server.
			// Form the TLAddress into a CalledPartyNumber for the transaction.
			// Attach calledParty and message body to the transaction.
			SipDialog::newSipDialogMOSMS(tran()->tranID(), tran()->subscriber(), toAddress, body.str(), contentType);
			return true;
		}
		case RPMessage::Ack:
		case RPMessage::SMMA:
			return true;
		case RPMessage::Error:
		default:
			return false;
	}
}

// see: Control::MOSMSController
MachineStatus MOSMSMachine::machineRunState(int state, const GSM::L3Message *l3msg, const SIP::DialogMessage *sipmsg)
{
		// See GSM 04.11 Arrow Diagram A5 for the transaction  (pat) NO, A5 is for GPRS.  Closest diagram is F1.
		//			SIP->Network message.
		// Step 1	MS->Network	CP-DATA containing RP-DATA with message
		// Step 2	Network->MS	CP-ACK
			// 4.11 6.2.2 State wait-for-RP-ACK, timer TR1M
		// Step 3	Network->MS	CP-DATA containing RP-ACK or RP-Error
		// Step 4	MS->Network	CP-ACK

		// LAPDm operation, from GSM 04.11, Annex F:
		// """
		// Case A: Mobile originating short message transfer, no parallel call:
		// The mobile station side will initiate SAPI 3 establishment by a SABM command
		// on the DCCH after the cipher mode has been set. If no hand over occurs, the
		// SAPI 3 link will stay up until the last CP-ACK is received by the MSC, and
		// the clearing procedure is invoked.
		// """

	WATCHF("MOSMS state=%x\n",state);
	PROCLOG2(DEBUG,state)<<LOGVAR(l3msg)<<LOGVAR(sipmsg)<<LOGVAR2("imsi",tran()->subscriber());
	switch (state) {
		case L3CASE_MM(CMServiceRequest): {
			timerStart(TCancel,30*1000,TimerAbortTran);	// Just in case.
			// This is both the start state and a request to start a new MO SMS when one is already in progress, as per GSM 4.11 5.4
			const L3CMServiceRequest *req = dynamic_cast<typeof(req)>(l3msg);
			const GSM::L3MobileIdentity &mobileID = req->mobileID();	// Reference ok - the SM is going to copy it.

			// FIXME: We only identify this the FIRST time.
			// The L3IdentifySM can check the MM state and just return.
			// FIXME: check provisioning
			return machPush(new L3IdentifyMachine(tran(),mobileID, &mIdentifyResult), stateIdentResult);
		}
		case stateIdentResult: {
			if (! mIdentifyResult) {
				//const L3CMServiceReject reject = L3CMServiceReject(L3RejectCause::Invalid_Mandatory_Information);
				// (pat 6-2014) I think this is wrong, based on comment below, so changing it to the main channel:
				// l3sendSms(L3CMServiceReject(L3RejectCause::Invalid_Mandatory_Information),SAPI0);
				MMRejectCause rejectCause = L3RejectCause::Invalid_Mandatory_Information;
				channel()->l3sendm(L3CMServiceReject(rejectCause),L3_DATA,SAPI0);
				return MachineStatus::QuitTran(TermCause::Local(rejectCause));
			}

			// Let the phone know we're going ahead with the transaction.
			// The CMServiceReject is on SAPI 0, not SAPI 3.
			PROCLOG(DEBUG) << "sending CMServiceAccept";
			// Update 8-6-2013: The nokia does not accept this message on SACCH SAPI 0 for in-call SMS;
			// so I am trying moving it to the main channel.
			//l3sendSms(GSM::L3CMServiceAccept(),SAPI0);
			channel()->l3sendm(GSM::L3CMServiceAccept(),L3_DATA,SAPI0);

			gReports.incr("OpenBTS.GSM.SMS.MOSMS.Start");
			return MachineStatusOK;
		}

#if FIXME
		case L3CASE_ERROR: {
			// (pat) TODO: Call this on parsel3 error...
			// TODO: Also send an error code to the sip side, if any.

			l3sendSms(CPError(getL3TI()));
			return MachineStatusQuitTran;
		}
#endif

		case L3CASE_SMS(DATA): {
			timerStop(TCancel);
			timerStart(TR1M,TR1Mms,TimerAbortTran);
			// Step 0: Wait for SAP3 to connect.
			// The first read on SAP3 is the ESTABLISH primitive.
			// That was done by our caller.
			//delete getFrameSMS(LCH,GSM::ESTABLISH);

			// Step 1: This is the first message: CP-DATA, containing RP-DATA.
			unsigned L3TI = l3msg->TI() | 0x08;
			tran()->setL3TI(L3TI);

			const CPData *cpdata = dynamic_cast<typeof(cpdata)>(l3msg);
			if (cpdata == NULL) {	// Currently this is impossible, but maybe someone will change the code later.
				l3sendSms(CPError(L3TI));
				return MachineStatus::QuitTran(TermCause::Local(L3Cause::SMS_Error));
			}

			// Step 2: Respond with CP-ACK.
			// This just means that we got the message and could parse it.
			PROCLOG(DEBUG) << "sending CPAck";
			l3sendSms(CPAck(L3TI));

			// (pat) The SMS message has already been through L3Message:parseL3, which called SMS::parseSMS(source), which manufactured
			// a CPMessage::CPData and called L3Message::parse() which called CPData::parseBody which called L3Message::parseLV,
			// which called CPUserData::parseV to leave the result in L3Message::CPMessage::CPData::mData.
			// As the mathemetician said after filling 3 blackboards with formulas: It is obvious!

			// FIXME -- We need to set the message ref correctly, even if the parsing fails.
			// The compiler gives a warning here.  Let it.  It will remind someone to fix it.
			// (pat) Update: If we cant parse far enough to get the ref we send a CPError that does not need the ref.
#if 0
			unsigned ref;
			bool success = false;
			try {
				// (pat) hierarchy is L3Message::CPMessage::CPData;  L3Message::parse calls CPData::parseBody.
				CPData data;
				data.parse(*CM);
				LOG(INFO) << "CPData " << data;
				// Transfer out the RPDU -> TPDU -> delivery.
				ref = data.RPDU().reference();
				// This handler invokes higher-layer parsers, too.
				success = handleRPDU(transaction,data.RPDU());
			}
			catch (SMSReadError) {
				LOG(WARNING) << "SMS parsing failed (above L3)";
				// Cause 95, "semantically incorrect message".
				LCH->l3sendf(CPData(L3TI,RPError(95,ref)),3);  if you ever use this, it should call l3sendSms
				delete CM;
				throw UnexpectedMessage();
			}
			catch (GSM::L3ReadError) {
				LOG(WARNING) << "SMS parsing failed (in L3)";
				delete CM;
				throw UnsupportedMessage();
			}
			delete CM;
#endif

			// Step 3
			// Send CP-DATA containing message ref and either RP-ACK or RP-Error.
			// If we cant parse the message, we send RP-Error immeidately, otherwise we wait for the dialog to finish one way or the other.
			const RLFrame &rpdu = cpdata->data().RPDU();
			this->mRpduRef = rpdu.reference();
			bool success = false;
			try {
				// This creates the outgoing SipDialog to send the message.
				success = handleRPDU(rpdu);
			} catch (...) {
				LOG(WARNING) << "SMS parsing failed (above L3)";
			}
			
			if (! success) {
				PROCLOG(INFO) << "sending RPError in CPData";
				// Cause 95 is "semantically incorrect message"
				l3sendSms(CPData(L3TI,RPError(95,mRpduRef)));
			}
			mSmsState = MoSmsWaitForAck;
			LOG(DEBUG) << "case DATA returning";
			return MachineStatusOK;
		}

		case L3CASE_SIP(dialogBye): {	// SIPDialog sends this when the MESSAGE clears.
			PROCLOG(INFO) << "SMS peer did not respond properly to dialog message; sending RPAck in CPData";
			l3sendSms(CPData(getL3TI(),RPAck(mRpduRef)));
			LOG(DEBUG) << "case dialogBye returning";
		}
		case L3CASE_SIP(dialogFail): {
			PROCLOG(INFO) << "sending RPError in CPData";
			// TODO: Map the dialog failure state to an RPError state.
			// Cause 127 is "internetworking error, unspecified".
			// See GSM 04.11 8.2.5.4 Table 8.4.
			l3sendSms(CPData(getL3TI(),RPError(127,mRpduRef)));
			LOG(DEBUG) << "case dialogFail returning";
		}

		case L3CASE_SMS(ACK): {
			timerStop(TR1M);
			// Step 4: Get CP-ACK from the MS.
			const CPAck *cpack = dynamic_cast<typeof(cpack)>(l3msg);
			PROCLOG(INFO) << "CPAck " << cpack;

			gReports.incr("OpenBTS.GSM.SMS.MOSMS.Complete");

			/* MOSMS RLLP request */
			//if (gConfig.getBool("Control.SMS.QueryRRLP")) {
				// Query for RRLP
				//if (!sendRRLP(mobileID, LCH)) {
				//	LOG(INFO) << "RRLP request failed";
				//}
			//}

			// Done.
			mSmsState = MoSmsMMConnection;
			// TODO: if (set) set->mmCallFinished();
			LOG(DEBUG) << "case ACK returning";
			// This attach causes any pending MT transactions to start now.
			gMMLayer.mmAttachByImsi(channel(),tran()->subscriberIMSI());
			return MachineStatus::QuitTran(TermCause::Local(L3Cause::SMS_Success));
		}

		default:
			LOG(DEBUG) << "unexpected state";
			return unexpectedState(state,l3msg);
	}
}

#if UNUSED
// Return the state of the current SMS procedure, if any.
SmsState getCurrentSMSState()
{
	MMContext *set = channel()->chanGetContext(true);
	TranEntry *sms = set->getTran(MMContext::TE_MOSMS1);
	if (! sms) { return SmsNonexistent; }

	MachineBase *base = sms->currentProcedure();
	if (base) {	// This should be an assert.
		MOSMSMachine *smssm = dynamic_cast<typeof(smssm)>(base);
		if (smssm) {	// This too.
			return smssm->mSmsState;
		}
	}
	return SmsNonexistent;
}
#endif

// There can be a max of two simultaneous MO-SMS.
// The CM Service Request to start a new MO-SMS during an existing one may arrive before the final ACK of the previous MO-SMS, as per GSM 4.11 5.4
// Therefore there are two MO-SMS possible: MMContext::TE_MOSMS1 is the old one and TE_MOSMS2 is the new one.
void startMOSMS(const GSM::L3MMMessage *l3msg, MMContext *mmchan)
{
	LOG(DEBUG) <<mmchan;
	//now we allocate below: TranEntry *tran = TranEntry::newMO(dcch, L3CMServiceType::ShortMessage);

	//MMContext *set = dcch->chanGetContext(true);
	RefCntPointer<TranEntry> prevMOSMS = mmchan->mmGetTran(MMContext::TE_MOSMS1);
	if (! prevMOSMS.self()) {
		// happiness.
		//set->setTran(MMContext::TE_MOSMS1,tran);
		//tran->teConnectChannel(dcch,TE_MOSMS1);
	} else {
		// Check for perfidy on the part of the MS: it cannot start a new MO-SMS unless the previous is nearly finished.
		//SmsState smsState = getCurrentSMSState();
		MachineBase *base = prevMOSMS->currentProcedure();
		bool badbunny = false;
		if (base) {	// This may happen if the MS is a bad bunny and sends two CM Sevice Requests effectively simultaneously.
			MOSMSMachine *smssm = dynamic_cast<typeof(smssm)>(base);
			if (! smssm || smssm->mSmsState != MoSmsWaitForAck) {
				badbunny = true;
			}
		} else {
			badbunny = true;
		}

		if (badbunny) {
			LOG(ERR) << "Received new MO-SMS before previous MO-SMS completed"<<LOGVAR2("MOSMS",prevMOSMS.self())<<l3msg;
			// Now what?  We've already got an SMS running...
			return;		// Just ignore it.
		}

		RefCntPointer<TranEntry> prevMOSMS2 = mmchan->mmGetTran(MMContext::TE_MOSMS2);
		if (prevMOSMS2.self()) {
			LOG(ERR) <<"Received third simultaneous MO-SMS, which is illegal:"<<LOGVAR2("MO-SMS1",prevMOSMS.self())<<LOGVAR2("MO-SMS2",prevMOSMS2.self());
			// Now what?  We could kill the oldest one or reject the new one.
			// Kill the oldest one, on the assumption that this indicates a bug in our code and that SMS is hung.
			prevMOSMS->teCancel(TermCause::Local(L3Cause::SMS_Error));	// Promotes TE_MOSMS2 to TE_MOSMS1
			devassert(mmchan->mmGetTran(MMContext::TE_MOSMS2) == NULL);
		}
		//mmchan->setTran(MMContext::TE_MOSMS2,tran);
		//tran->teConnectChannel(mmchan,MMContext::TE_MOSMS2);
	}
	TranEntry *tran = TranEntry::newMOSMS(mmchan);

	// Fire up an SMS state machine for this transaction.
	MOSMSMachine *mocp = new MOSMSMachine(tran);
	// The message is CMServiceRequest.
	tran->lockAndStart(mocp,(GSM::L3Message*)l3msg);
}

// Return true on success.
bool MTSMSMachine::createRPData(RPData &rp_data)
{
	// TODO: Read MIME Type from smqueue!!
	const char *contentType = tran()->mContentType.c_str();
	PROCLOG(DEBUG)<<LOGVAR(contentType)<<LOGVAR(tran()->mMessage);
	if (strncmp(contentType,"text/plain",10)==0) {
		TLAddress tlcalling = TLAddress(tran()->calling().digits());
		TLUserData tlmessage = TLUserData(tran()->mMessage.c_str());
		PROCLOG(DEBUG)<<LOGVAR(tlcalling)<<LOGVAR(tlmessage);
		rp_data = RPData(this->mRpduRef,
			RPAddress(gConfig.getStr("SMS.FakeSrcSMSC").c_str()),
			TLDeliver(tlcalling,tlmessage,0));
	} else if (strncmp(contentType,"application/vnd.3gpp.sms",24)==0) {
		BitVector2 RPDUbits(strlen(tran()->mMessage.c_str())*4);
		if (!RPDUbits.unhex(tran()->mMessage.c_str())) {
			LOG(WARNING) << "Message is zero length which is valid";
			// This is valid continue
			return true;
		}

		try { // I suspect this is here to catch the above FIXED crash when string is zero length
			RLFrame RPDU(RPDUbits);
			LOG(DEBUG) << "SMS RPDU: " << RPDU;

			rp_data.parse(RPDU);
			LOG(DEBUG) << "SMS RP-DATA " << rp_data;
		}
		catch (SMSReadError) {
			LOG(WARNING) << "SMS parsing failed (above L3)";
			// Cause 95, "semantically incorrect message".
			//LCH->l2sendf(CPData(L3TI,RPError(95,this->mRpduRef)),3); if you ever use this, it should call l3sendSms
			return false;
		}
		catch (GSM::L3ReadError) {
			LOG(WARNING) << "SMS parsing failed (in L3)";
			// TODO:: send error back to the phone
			return false;
		}
		catch (...) {
			LOG(ERR) << "Unexpected throw";	// cryptic, but should never happen.
			return false;
		}
	} else {
		LOG(WARNING) << "Unsupported content type (in incoming SIP MESSAGE) -- type: " << contentType;
		return false;
	}
	return true;
}

MachineStatus MTSMSMachine::machineRunState1(int state,const L3Frame*frame,const L3Message*l3msg, const SIP::DialogMessage*sipmsg)
{
		// Step 1	Network->MS	CP-DATA containing RP-DATA with message
		// Step 2	MS->Network	CP-ACK
			// 4.11 6.2.2 State wait-to-send-RP-ACK, timer TR2M
		// Step 3	MS->Network	CP-DATA containing RP-ACK or RP-Error
		// Step 4	Network->MS	CP-ACK
		//			Network->SIP response.

	PROCLOG2(DEBUG,state)<<LOGVAR(l3msg)<<LOGVAR(sipmsg)<<LOGVAR2("imsi",tran()->subscriber());
	switch(state) {
		case stateStart: {
			// There is no dialog for a SMS initiated on this BTS.
			if (getDialog() && getDialog()->isFinished()) {
				// SIP side closed already.
				// We can no longer inform the SIP side whether we succeed or not.
				// Should we continue and deliver the message to the MS or not?
				return MachineStatus::QuitTran(TermCause::Local(L3Cause::SMS_Timeout));	// could be a sip internal error?
			}
			timerStart(TR2M,TR2Mms,TimerAbortTran);

			// Allocate Transaction Identifier
			unsigned l3ti = channel()->chanGetContext(true)->mmGetNextTI();
			tran()->setL3TI(l3ti);
			setGSMState(CCState::SMSDelivering);

			this->mRpduRef = random() % 255;

			gReports.incr("OpenBTS.GSM.SMS.MTSMS.Start");

			// pat 6-2014.  We just send the ESTABLISH_REQUEST no matter what now.
			// The LAPDm will respond with ESTABLISH_INDICATION immediately if 
			SAPI_t sap = getSmsSap();
			//L3LogicalChannel *smschan = getSmsChannel();
			//if (smschan->multiframeMode(3)) { goto step1; }		// If already established.
			// if (channel()->multiframeMode(sap)) { goto step1; }		// If already established.

			// Start ABM in SAP3.
			//smschan->l3sendp(GSM::L3_ESTABLISH_REQUEST,SAPI3);
			channel()->l3sendp(GSM::L3_ESTABLISH_REQUEST,sap);
			// Wait for SAP3 ABM to connect.
			// The next read on SAP3 should be the ESTABLISH primitive.
			// This won't return NULL.  It will throw an exception if it fails.
			// (pat) WARNING: Previous code waited for a return ESTABLISH,
			// but I think the l3sendp(ESTABLISH) will hang until this happens so it is now a no-op.
			// delete getFrameSMS(LCH,GSM::ESTABLISH);
			LOG(DEBUG) << "case start returning, after sending ESTABLISH";
			return MachineStatusOK;	// Wait for the ESTABLISH on the uplink.
		}

		// We use ESTABLISH_INDICATION instead of ESTABLISH_CONFIRM to indicate establishment.
		// We would have to accept both ESTABLISH_CONFIRM and ESTABLISH_INDICATION here anyway in case
		// SABM was started by us and handset simultaneously, so we just dont bother with making ESTABLISH_CONFIRM separate.
		case L3CASE_PRIMITIVE(L3_ESTABLISH_INDICATION):
		case L3CASE_PRIMITIVE(L3_ESTABLISH_CONFIRM): {
			// Step 1
			// Send the first message.
			// CP-DATA, containing RP-DATA.

			RPData rp_data;
			int l3ti = getL3TI();
			if (! createRPData(rp_data)) { // NULL can be returned
				l3sendSms(CPData(l3ti,RPError(95,this->mRpduRef)));
				// TODO: Is this correct? 
				// TODO: Make sure MachineStatusQuitTran sends a failure code to SIP.
				if (getDialog()) getDialog()->MTSMSReply(400, "Bad Request");
				return MachineStatus::QuitTran(TermCause::Local(L3Cause::SMS_Error));
			}

			CPData deliver(l3ti,rp_data);
			PROCLOG(INFO) << "sending " << deliver;
	// WORKING: MS Does not respond to this message.
	// (pat) FIXME: The MS may send a DELIVER_REPORT which is discarded by parseTPDU.
			l3sendSms(deliver);
			LOG(DEBUG) << "case ESTABLISH returning, after receiving ESTABLISH";
			return MachineStatusOK; // Wait for CP-ACK message.
		}

		// Step 2
		// Get the CP-ACK.
		case L3CASE_SMS(ACK): {
			// FIXME -- Check reference and check for RPError.
			return MachineStatusOK;	// Now we are waiting for CP-DATA.
		}

		// Step 3
		// Get CP-DATA containing RP-ACK and message reference.
		case L3CASE_SMS(DATA): {
			timerStop(TR2M);
			PROCLOG(DEBUG) << "MTSMS: data from MS " << *l3msg;
			// FIXME -- Check L3 TI.

			// Parse to check for RP-ACK.
			// We already called parsel3 on the message.
			//CPData data;
			//try {
			//	data.parse(*CM);
			//	LOG(DEBUG) << "CPData " << data;
			//}
			//catch (SMSReadError) {
			//	LOG(WARNING) << "SMS parsing failed (above L3)";
			//	// Cause 95, "semantically incorrect message".
			//	LCH->l2sendf(CPError(L3TI,95),3);
			//	throw UnexpectedMessage();
			//}
			//catch (GSM::L3ReadError) {
			//	LOG(WARNING) << "SMS parsing failed (in L3)";
			//	throw UnsupportedMessage();
			//}
			//delete CM;

			const CPData *cpdata = dynamic_cast<typeof(cpdata)>(l3msg);

			// FIXME -- Check SMS reference.

			bool success = true;
			if (cpdata->RPDU().MTI()!=RPMessage::Ack) {
				PROCLOG(WARNING) << "unexpected RPDU " << cpdata->RPDU();
				success = false;
			}

			gReports.incr("OpenBTS.GSM.SMS.MTSMS.Complete");

			// Step 4
			// Send CP-ACK to the MS.
			PROCLOG(INFO) << "MTSMS: sending CPAck";
			l3sendSms(CPAck(getL3TI()));

			// Ack in SIP domain.
			if (!getDialog()) {
				LOG(DEBUG) << "No dialog found for MTSMS; could be welcome message, CLI SMS, or Dialog pre-destroyed error";
			} else if (success) {
				getDialog()->MTSMSReply(200,"OK");
			} else {
				getDialog()->MTSMSReply(400, "Bad Request");
			}

			LOG(DEBUG) << "case DATA returning";
			return MachineStatus::QuitTran(TermCause::Local(L3Cause::SMS_Success));	// Finished.
		}
		default:
			return unexpectedState(state,l3msg);
	}
}

void initMTSMS(TranEntry *tran)
{
	tran->teSetProcedure(new MTSMSMachine(tran));
	//tran->lockAndStart(new MTSMSMachine(tran));
}

#if UNUSED 	// (pat) what was I thinking here? 
xxxxxxxx This version is not used
// Parse an incoming SMS message into RPData, save everything else we need.
// We do this immediately upon reception of a SIP message to error check it before queueing it for delivery to an MS.
// Return result or NULL on failure.  SIP should return error 400 "Bad Request" in this case.
RPData *parseSMS(const char *callingPartyDigits, const char* message, const char* contentType)
{
	// TODO: Read MIME Type from smqueue!!
	unsigned reference = random() % 255;
	RPData *rp_data = NULL;

	if (strncmp(contentType,"text/plain",10)==0) {
		rp_data = new RPData(reference,
			RPAddress(gConfig.getStr("SMS.FakeSrcSMSC").c_str()),
			TLDeliver(callingPartyDigits,message,0));
	} else if (strncmp(contentType,"application/vnd.3gpp.sms",24)==0) {
		BitVector2 RPDUbits(strlen(message)*4);
		if (!RPDUbits.unhex(message)) {
			LOG(WARNING) << "Hex string parsing failed (in incoming SIP MESSAGE)";
			return NULL;
		}

		try {
			RLFrame RPDU(RPDUbits);
			LOG(DEBUG) << "SMS RPDU: " << RPDU;

			rp->data = new RPData();
			rp_data->parse(RPDU);
			LOG(DEBUG) << "SMS RP-DATA " << rp_data;
		}
		catch (SMSReadError) {
			LOG(WARNING) << "SMS parsing failed (above L3)";
			delete rp_data; rp_data = NULL;
		}
		catch (GSM::L3ReadError) {
			LOG(WARNING) << "SMS parsing failed (in L3)";
			delete rp_data; rp_data = NULL;
		}
		catch (...) {		// Should not happen, but be safe.
			LOG(WARNING) << "SMS parsing failed (unexpected error)";
			delete rp_data; rp_data = NULL;
		}
		return rp_data;
	} else {
		LOG(WARNING) << "Unsupported content type (in incoming SIP MESSAGE) -- type: " << contentType;
		return NULL;
	}
}
#endif

};
