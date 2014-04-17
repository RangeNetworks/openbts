/*
* Copyright 2013, 2014 Range Networks, Inc.
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
// Written by Pat Thompson.


#define LOG_GROUP LogGroup::SIP		// Can set Log.Level.SIP for debugging

#include "SIPBase.h"
#include "SIPDialog.h"
#include "SIP2Interface.h"
#include "SIPTransaction.h"
//#include <ControlTransfer.h>
#include <L3TranEntry.h>
#include <L3StateMachine.h>
#include <GSML3MMElements.h>	// for L3CMServiceType
#include <algorithm>

namespace SIP {
using namespace Control;
SipDialog *gRegisterDialog = NULL;

SipDialog *getRegistrar()
{
	if (gRegisterDialog == NULL) {
		gRegisterDialog = SipDialog::newSipDialogRegister1();
	} else {
		// This allows the user to change SIP.Proxy.Registration from the CLI.
		gRegisterDialog->updateProxy("SIP.Proxy.Registration");
	}
	return gRegisterDialog;
}

void SipDialog::dgReset()
{
	mPrevDialogState = DialogState::dialogUndefined; sipStopTimers();
	//mDownlinkFifo.clear();
}


void SipDialog::MODSendBYE()
{
	LOG(INFO) <<sbText();

	setSipState(MODClearing);
	SipMOByeTU *byeTU = new SipMOByeTU(this);
	byeTU->sctStart();
}

void SipDialog::sendInfoDtmf(unsigned bcdkey)
{
	// Has a previous DTMF not finished yet?

	// Start a new Sip INFO Transaction to send the key off.
	SipDtmfTU *dtmfTU = new SipDtmfTU(this,bcdkey);
	dtmfTU->sctStart();
}

// (pat) This is the post-l3-rewrite way, most initialization during construction.
SipDialog *SipDialog::newSipDialogMT(DialogType dtype, SipMessage *req)
{
	LOG(DEBUG);
	assert(dtype == SIPDTMTC || dtype == SIPDTMTSMS);
	string proxy = req->smGetProxy();	// Get it from the top via.
	if (proxy.empty()) {
		LOG(ERR) << "Missing proxy (from top via) in MT SIP message:"<<req;
		// Guess at a proxy and try to keep going.
		proxy = gConfig.getStr(dtype == SIPDTMTSMS ? "SIP.Proxy.SMS" : "SIP.Proxy.Speech");
	}
	SipDialog *dialog = new SipDialog(dtype,proxy,"INVITE or MESSAGE via");
	// 2-2014: RFC 3267 8.2.6.2 says the UAS (sip server) MUST add a "to" tag to a response, and MAY add a "to" tag to a provisional (100) response.
	// The reason is in case the request is forked, the client could distinguish responses from multiple servers, a case that would not happen for us.
	dialog->dsSetLocalHeaderMT(&req->msmTo,dtype == SIPDTMTC);
	dialog->dsSetRemoteHeader(&req->msmFrom);
	//dialog->mSipUsername = req->smUriUsername();	// IMSI/TMSI is in both the URI and the To: header.
	// TODO: Validate username - must be valid IMSI or TMSI.
	ScopedLock lock(dialog->mDialogLock,__FILE__,__LINE__);	// probably unnecessary.
	dialog->dsSetCallId(req->msmCallId);
	dialog->mSdpOffer = req->msmBody;	// Only useful for MTC, a no-op for MTSMS.
	dialog->saveInviteOrMessage(req,false);
	gSipInterface.dmAddCallDialog(dialog);
	return dialog;
}

// There is just one SipDialog that handles all REGISTER requests.
SipDialog *SipDialog::newSipDialogRegister1() 		// caller imsi
{
	LOG(DEBUG);
	SipDialog *dialog = new SipDialog(SIPDTRegister,gConfig.getStr("SIP.Proxy.Registration"),"SIP.Proxy.Registration");
	// RFC3261 10.2: REGISTER fields are different from normal requests.
	// The Request URL is the IP address (only) of the Registrar.
	// The To: is the 'address of record' formatted as a SIP URI.
	// The From: is the 'responsible party' and is equal to To: unless it is a third-party registration.
	// What about tags?  I dont think it needs them because it is not a dialog creating request, but we add them
	// anyway and it hasn't hurt anything.
	dialog->dsSetCallId(globallyUniqueId(""));
	gSipInterface.dmAddCallDialog(dialog);
	return dialog;
}

// Open an MOSMS [Mobile Originated Short Message Service] SIP Transaction and send the invite.
// We use a dialog for this even though it is just a message because it was easier to interface
// to the Control directory without changing anything.
SipDialog *SipDialog::newSipDialogMOSMS(
	TranEntryId tranid,
	const FullMobileId &fromMsId,		// caller imsi
	const string &calledDigits,		// number being called, or it may be config option SIP.SMSC
	const string &body,
	const string &contentType)
{
	LOG(DEBUG) <<LOGVAR(fromMsId)<<LOGVAR2("called",calledDigits); //<<LOGVAR2("tranid",wTranId);
	// This is weird - use the local IP address as the domain of the remote user?
	SipDialog *dialog = new SipDialog(SIPDTMOSMS,gConfig.getStr("SIP.Proxy.SMS"),"SIP.Proxy.SMS");
	dialog->dsSetLocalMO(fromMsId,gPeerIsBuggySmqueue ? true : false);
	string calledDomain = dialog->localIP();
	if (gConfig.getStr("SIP.Realm").length() > 0) {
		string tmpURI = makeUri(calledDigits,calledDomain);
		tmpURI.erase(std::remove(tmpURI.begin(), tmpURI.end(), '+'), tmpURI.end());
		dialog->dsSetRemoteUri(tmpURI);
	} else {
		dialog->dsSetRemoteUri(makeUri(calledDigits,calledDomain));
	}
	dialog->smsBody = body;				// Temporary until smqueue is fixed.
	dialog->smsContentType = contentType;		// Temporary until smqueue is fixed.

	// Must lock once we do dmAddCallDialog to prevent the SIPInterface threads from accessing this dialog
	// while we finish construction.
	ScopedLock lock(dialog->mDialogLock,__FILE__,__LINE__);
	gSipInterface.dmAddCallDialog(dialog);
	//dialog->MOSMSSendMESSAGE(calledDigits,calledDomain,body,contentType);
	gNewTransactionTable.ttSetDialog(tranid,dialog);		// Must do this before the dialog receives any messages.
	dialog->MOSMSSendMESSAGE(body,contentType);
	return dialog;
}

SipDialog *SipDialog::newSipDialogMOUssd(
	TranEntryId tranid,
	const FullMobileId &fromMsId,		// caller imsi
	const string &wUssd,			// USSD string entered by user to send to network.
	L3LogicalChannel *chan
	)
{
	LOG(DEBUG) << "MOUssd (INVITE)"<<LOGVAR(fromMsId)<<LOGVARM(wUssd);
	// TODO: The SIPEngine constructor calls sipSetUser.  FIX IT.  Maybe I just need to replace SIPEngine.
	const char *proxyOption = "SIP.Proxy.USSD";
	string proxy = gConfig.getStr(proxyOption);
	LOG(DEBUG) << LOGVAR(proxyOption) <<LOGVAR(proxy);
	if (proxy.length() > 259) {	// TODO: This should be in the config checker, if anywhere.
		LOG(ALERT) << "Configured " <<proxyOption <<" hostname is greater than 253 bytes!"; 
	} 
	SipDialog *dialog = new SipDialog(SIPDTMOUssd,proxy,proxyOption);
	dialog->dsSetLocalMO(fromMsId,true);
	gReports.incr("OpenBTS.SIP.INVITE.Out");
	// Must lock once we do dmAddCallDialog to prevent the SIPInterface threads from accessing this dialog
	// while we finish construction.
	ScopedLock lock(dialog->mDialogLock,__FILE__,__LINE__);	// Must lock before dmAddCallDialog.

	if (proxy == "testmode") {
		gNewTransactionTable.ttSetDialog(tranid,dialog);		// Must do this before the dialog receives any messages.
		DialogUssdMessage *dmsg = new DialogUssdMessage(tranid,DialogState::dialogBye,0);
		dmsg->dmMsgPayload = "Hello from OpenBTS.  You entered:"+wUssd;
		LOG(DEBUG) << "USSD test mode"<<LOGVAR(chan)<<LOGVAR(tranid)<<LOGVAR(fromMsId)<<dmsg->dmMsgPayload;
		dialog->dialogQueueMessage(dmsg);
		return dialog;
	}
	dialog->dsSetRemoteUri(makeUri(wUssd,dialog->localIP()));
	// TODO: What about codecs?  The example in 24.390 annex A has them.

	gSipInterface.dmAddCallDialog(dialog);
	gNewTransactionTable.ttSetDialog(tranid,dialog);		// Must do this before the dialog receives any messages.
	dialog->MOUssdSendINVITE(wUssd,chan);
	return dialog;
}

// Open an MOC [Mobile Originated Call] dialog and send the invite.
SipDialog *SipDialog::newSipDialogMOC(
	TranEntryId tranid,
	const FullMobileId &fromMsId,			// caller imsi
	const string &wCalledDigits,		// number being called, or empty for an emergency call.
	CodecSet wCodecs,				// phone capabilities
	L3LogicalChannel *chan
	)
{

#ifdef C_CRELEASE
	LOG(DEBUG) << "MOC SIP (INVITE)"<<LOGVAR(fromMsId)<<LOGVAR2("called",wCalledDigits) <<LOGVAR(isEmergency);
	// TODO: The SIPEngine constructor calls sipSetUser.  FIX IT.  Maybe I just need to replace SIPEngine.
	const char *proxyOption = isEmergency ? "SIP.Proxy.Emergency" : "SIP.Proxy.Speech";
#else
	LOG(DEBUG) << "MOC SIP (INVITE)"<<LOGVAR(fromMsId)<<LOGVAR2("called",wCalledDigits);
	// TODO: The SIPEngine constructor calls sipSetUser.  FIX IT.  Maybe I just need to replace SIPEngine.
	const char *proxyOption = "SIP.Proxy.Speech";
#endif

	string proxy = gConfig.getStr(proxyOption);
	LOG(DEBUG) << LOGVAR(proxyOption) <<LOGVAR(proxy);
	if (proxy.length() > 259) {	// TODO: This should be in the config checker, if anywhere.
		LOG(ALERT) << "Configured " <<proxyOption <<" hostname is greater than 253 bytes!"; 
	} 

	SipDialog *dialog = new SipDialog(SIPDTMOC,proxy,proxyOption);
	dialog->dsSetLocalMO(fromMsId,true);

	{
		gReports.incr("OpenBTS.SIP.INVITE.Out");
		dialog->dsSetRemoteUri(makeUri(wCalledDigits,dialog->localIP()));
	}

	dialog->mRTPPort = Control::allocateRTPPorts();
	dialog->mCodec = wCodecs;

	// Must lock once we do dmAddCallDialog to prevent the SIPInterface threads from accessing this dialog
	// while we finish construction.
	ScopedLock lock(dialog->mDialogLock,__FILE__,__LINE__);	// Must lock before dmAddCallDialog.
	gSipInterface.dmAddCallDialog(dialog);
	gNewTransactionTable.ttSetDialog(tranid,dialog);		// Must do this before the dialog receives any messages.
	dialog->MOCSendINVITE(chan);
	return dialog;
}

// This is called in BS2 after a handover complete is received.  It is an inbound handover, but an outoing MO re-INVITE.
// We take the SIP REFER message created by BS1 and send it to the SIP server as a re-INVITE.
// Note that the MS may go from BS1 to BS2 and back to BS1, in which case there may
// already be an existing dialog in some non-Active state.
SipDialog *SipDialog::newSipDialogHandover(TranEntry *tran, string sipReferStr)
{
	LOG(DEBUG)<<LOGVAR(tran) <<LOGVAR(sipReferStr);
	static const string inviteStr("INVITE");

	// Init the Dialog State from the SIP REFER message.
	SipMessage *msg = sipParseBuffer(sipReferStr.c_str());
	if (msg == NULL) { return NULL; }	// Message already printed.
	SipUri referto(msg->msmHeaders.paramFind("Refer-To"));
	string proxy = referto.uriHostAndPort();
	// 7-23 wrong: SipDialog *dialog = new SipDialog(SIPDTMTC,proxy);
	SipDialog *dialog = new SipDialog(SIPDTMOC,proxy,"REFER message");
	dialog->mIsHandover = true;
	dialog->dsSetRemoteHeader(&msg->msmTo);
	dialog->dsSetLocalHeader(&msg->msmFrom);
	dialog->dsSetCallId(msg->msmCallId);
	// TODO: If any other intervening messages were sent by BTS1 between the REFER and now the CSeqNum will not be correct.
	dialog->mLocalCSeq = msg->msmCSeqNum + 1;
	// We copied the peer SDP we got from the SIP server into the handover message passed from BS1 to BS2;
	// I dont think we need to save sdpResponse here - we are going to use it for the last time immediately below.
	dialog->mCodec = tran->getCodecs();			// TODO: We need to renegotiate this, or set it from SDP.  There is no point even setting this here.

	// Get remote RTP from SIP REFER message, init RTP, create new SDP offer from previous SDP response.
	// The incoming SDP has the codec previously negotiated, so it should still be ok.
	dialog->mRTPPort = Control::allocateRTPPorts();
	SdpInfo sdpRemote;
	sdpRemote.sdpParse(msg->msmBody);
	SdpInfo sdpLocal = sdpRemote;	// In particular, we are copying the sessionId and versionId.
	// Send our local RTP port to the SIP server.
	sdpLocal.sdpRtpPort = dialog->mRTPPort;
	sdpLocal.sdpHost = dialog->localIP();
	dialog->mSdpOffer = sdpLocal.sdpValue();

	// Make the re-INVITE
	SipMessage *invite = dialog->makeInitialRequest(inviteStr);
	invite->smAddBody(string("application/sdp"),dialog->mSdpOffer);

	// Send it off.
	ScopedLock lock(dialog->mDialogLock,__FILE__,__LINE__);
	gSipInterface.dmAddCallDialog(dialog);
	dialog->moWriteLowSide(invite);
	delete invite;	// moWriteLowSide saved a copy of this.
	dialog->setSipState(HandoverInbound);

	return dialog;
}


SipDialog::~SipDialog()
{
	// nothing
}

TranEntry *SipDialog::findTranEntry()
{
	if (this->mTranId == 0) {
		// No attached transaction.  Can happen if we jumped the gun (the dialog is created before the transaction
		// and there could be a race with an incoming message) or if we responded with an early error
		// to a dialog and never created a transaction for it, for example, 486 Busy Here.
		return NULL;
	}
	return gNewTransactionTable.ttFindById(this->mTranId);
}


TranEntry *SipDialog::createMTTransaction(SipMessage *invite)
{
	// Create an incipient TranEntry.  It does not have a TI yet.
	TranEntry *tran = NULL;
	string callerId;
	if (gConfig.getStr("GSM.CallerID.Source").compare("username") == 0) {
		callerId = sipRemoteUsername();
		LOG(INFO) << "source=username, callerId = " << callerId;
	} else if (gConfig.getStr("GSM.CallerID.Source").compare("p-asserted-identity") == 0) {
		string tmpcid = invite->msmHeaders.paramFind("P-Asserted-Identity");
		unsigned first = tmpcid.find("<sip:");
		unsigned last = tmpcid.find_last_of("@");
		callerId = tmpcid.substr(first+5, last-first-5);
		LOG(INFO) << "source=p-asserted-identity, callerId = " << callerId;
	} else {
		callerId = sipRemoteDisplayname();
		LOG(INFO) << "source=username, callerId = " << callerId;
	}
	FullMobileId msid;
	msid.mImsi = invite->smGetInviteImsi();
	if (invite->isINVITE()) {
		tran = TranEntry::newMTC(this,msid,GSM::L3CMServiceType::MobileTerminatedCall,callerId);
		// Tell the sender we are trying.
		this->MTCSendTrying();
	} else {
		assert(0);
	}
	return tran;
}

// If the cause is handoverOutbound, kill the dialog now: dont send a BYE, dont wait for any other incoming messsages.
// Used for outbound handover, where the SIP session was transferred to another BTS.
void SipDialog::dialogCancel(CancelCause cause)
{
	WATCH("dialogCancel"<<LOGVAR(getSipState())<<LOGVAR(cause) );
	ScopedLock lock(mDialogLock,__FILE__,__LINE__);

	SIP::SipState state = this->getSipState();
	LOG(INFO) << dialogText(); // "SIP state " << state;

	switch (cause) {
		case CancelCauseHandoverOutbound:
		case CancelCauseSipInternalError:
			// Terminate the dialog instantly.  Dont send anything on the SIP interface.
			sipStopTimers();
			// We need to remove the callid of the terminated outbound dialog queue from SIPInterface in case
			// the same call is handerovered back, it would then be a duplicate.
			gSipInterface.dmRemoveDialog(this);
			return;
		default:
			break;
	}

	//why aren't we checking for failed here? -kurtis ; we are now. -david
	if (this->sipIsFinished()) return;
	switch (mDialogType) {
	case SIPDTRegister:
	case SIPDTUnregister:
		// The Register is not a full dialog so we dont send anything when we cancel.
		break;
	case SIPDTMOSMS:
	case SIPDTMTSMS:
	case SIPDTMOUssd:
		setSipState(Cleared);
		break;
	case SIPDTMTC:
	case SIPDTMOC:
		switch (state) {
			case SSTimeout:
			case MOSMSSubmit:	// Should never see a message state in an INVITE dialog.
				LOG(ERR) "Unexpected SIP State:"<<state;
				break;
			case Active:			// (pat) MOC received OK; MTC sent ACK
				//Changes state to clearing
				this->MODSendBYE();
				//then cleared
				sipStartTimers(); // formerly: MODWaitForBYEOK();
				break;
			case SSNullState:	// (pat) MTC initial state - nothing sent yet. MOC not used because sends INVITE on construction.
			case Starting:		// (pat) MOC or MOSMS or inboundHandover sent INVITE; MTC not used.
			case Proceeding:	// (pat) MOC received Trying, Queued, BeingForwarded; MTC sent Trying
			case Ringing:		// (pat) MOC received Ringing, notably not used for MTC sent Ringing.
			case MOCBusy:		// (pat) MOC received Busy; MTC not used.
			case Connecting:		// (pat) MTC sent OK.
			case HandoverInbound:
				if (mDialogType == SIPDTMOC) {
					// To cancel the invite before the ACK is received we must send CANCEL instead of BYE.
					this->MODSendCANCEL(); //Changes state to MODCanceling
				} else {
					// We are the INVITE recipient server and have not received the ACK yet, so we must send an error response.
					// Yes this was formerly used for MTC also.  TODO: Make sure it works!
					// RFC3261 (SIP) is internally inconsistent describing the error codes - the 4xxx and 5xx generic
					// descriptions are contracted by specific error code descriptions.
					// This is from Paul Chitescu at Null Team:
					// "A 504 Server Timeout seems the most adequate response [to MS not responding to page.]
					// 408 is reserved for SIP protocol timeouts (no answer to SIP message)
					// 504 indicates some other timeout beyond SIP (interworking)
					// 480 indicates some temporary form of resource unavailability or congestion but 
					// resource is accessible and can be checked"
					// 486 "Busy Here" implies that we found the MS but it really is busy.
					// 503 indicates the service is unavailable but does not imply for how long
					// TODO: We should probably send different codes for different reasons.
					// Note: We previously sent 480.
					//this->MTCEarlyError(480, "Temporarily Unavailable"); // The message must be 300-699.

					int sipcode = 500; const char *reason = "Server Internal Error";
					switch (cause) {
						case CancelCauseHandoverOutbound:
						case CancelCauseSipInternalError:
							assert(0);		// handled above
						case CancelCauseBusy:			// MS is here and unavailable.
						case CancelCauseUnknown:		// Loss of contact with MS or an error.
						case CancelCauseCongestion:		// MS is here but no channel avail or other congestion.
							// The MS is here but we cannot get at it for some reason.
							sipcode = 486; reason = "Busy Here";
							break;
						case CancelCauseNoAnswerToPage:	// We dont have any clue if the MS is in this area or not.
							// The MS is not here or turned off.
							sipcode = 504; reason = "Temporarily Unavailable";
							break;
						case CancelCauseOperatorIntervention:
							sipcode = 487; reason = "Request Terminated Operator Intervention";
							break;
					}
					this->MTCEarlyError(sipcode,reason); // The message must be 300-699.
				}
				break;
			case MODClearing:	// (pat) MOD sent BYE
			case MODCanceling:	// (pat) MOD sent a cancel, see forceSIPClearing.
			case MODError:		// (pat) MOD sent an error response, see forceSIPClearing.
			case MTDClearing:	// (pat) MTD received BYE.
			case MTDCanceling:	// (pat) MTD received CANCEL
			case Canceled:		// (pat) received OK to CANCEL.
			case Cleared:		// (pat) MTD sent OK to BYE, or MTD internal error loss of FIFO, or MOSMS received OK, or MTSMS sent OK.
			case SSFail:
				// Some kind of clearing already in progress.  Do not repeat.
				break;
			case HandoverOutbound:	// We never used this state.
				// Not sure what to do with these.
				break;
		}
		break;
	default:
		assert(0);
		break;
	}
}

void SipEngine::dialogQueueMessage(DialogMessage *dmsg)
{
	// This was used when there was just one layer3 thread:
	// TODO: We may still use this for UMTS.
	//Control::gCSL3StateMachine.csl3Write(new Control::GenericL3Msg(dmsg,callID()));
	// Now we enqueue dialog messages in a queue in their dialog, and let L3 fish it out from there.
	// We dont enqueue on the GSM LogicalChannel because that may change from, eg, SDCCH to FACCH before this message is processed.
	LOG(DEBUG) << "sending DialogMessage to L3 " /*<<dialogText()*/ <<LOGVAR(dmsg);
	//mDownlinkFifo.write(dmsg);
	gNewTransactionTable.ttAddMessage(mTranId,dmsg);
}

bool SipDialog::permittedTransition(DialogState::msgState oldState, DialogState::msgState newState)
{
	if (newState > oldState) { return true; }	// That was easy!
	if (newState == oldState) {
		// Allow multiple proceeding/ringing notifications:
		if (newState == DialogState::dialogProceeding || newState == DialogState::dialogRinging) { return true; }
	}
	return false;
}

void SipDialog::dialogPushState(
	SipState newSipState,				// The new sip state.
	int code,						// The SIP message code that caused the state change, or 0 for ACK or total failures.
	char timer)
{
	SipState oldSipState = getSipState();
	DialogState::msgState oldDialogState = getDialogState();
	setSipState(newSipState);

	// If it is a new state, inform L3.
	DialogState::msgState nextDialogState = getDialogState();	// based on the newSipState we just set.
	LOG(DEBUG) <<LOGVAR(oldSipState)<<LOGVAR(newSipState)<<LOGVAR(getSipState())<<LOGVAR(mPrevDialogState)<<LOGVAR(oldDialogState)<<LOGVAR(nextDialogState)<<dialogText();
	if (nextDialogState == DialogState::dialogStarted) {
		// This state is used for MO transactions just to indicate the dialog is active,
		// but the MO state machine already knows that since it created the dialog,
		// so we dont return this state as a notification.
		return;
	}
	if (permittedTransition(mPrevDialogState,nextDialogState)) {
		DialogMessage *dmsg = new DialogMessage(mTranId,nextDialogState,code);
		dialogQueueMessage(dmsg);
	} else {
		LOG(DEBUG) << "no dialog state change";
	}
	mPrevDialogState = nextDialogState;

	// A timer may be specified if the SIP state is one indicating failure.
	// The timer letter corresponds to one of those specified in RFC3261, and specifies the dialog
	// should not be destroyed until the timer expires.
	switch (timer) {
		case 0:
			break;	// default, no timer specified.
		case 'D':
			// RFC3261 17.1.1.2 says set Timer D to 32s instead of 64*T1.  Whatever.
			if (dsPeer()->ipIsReliableTransport()) {
				mTimerD.setOnce(32000);
			}
			break;
		case 'K':
			mTimerK.setOnce(T4);
			break;
		default: assert(0);
	}
}


void SipDialog::dialogChangeState(
	SipMessage *sipmsg)						// The message that caused the state change, or NULL for total failures.
{
	dialogPushState(getSipState(),sipmsg?sipmsg->smGetCode():0);
	//LOG(DEBUG) <<dialogText();
	//// If it is a new state, inform L3.
	//DialogState::msgState nextDialogState = getDialogState();
	//if (nextDialogState == DialogState::dialogStarted) {
	//	// This state is used for MO transactions just to indicate the dialog is active,
	//	// but the MO state machine already knows that since it created the dialog,
	//	// so we dont return this state as a notification.
	//	return;
	//}
	//if (permittedTransition(mPrevDialogState,nextDialogState)) {
	//	unsigned code = sipmsg ? sipmsg->smGetCode() : 0;
	//	DialogMessage *dmsg = new DialogMessage(mTranId,nextDialogState,code);
	//	// done by the register TU
	//	dialogQueueMessage(dmsg);
	//} else {
	//	LOG(DEBUG) << "no dialog state change";
	//}
	//mPrevDialogState = nextDialogState;
}

// Only a small subset of SIP states are passed to the L3 Control layer as dialog states.
DialogState::msgState SipDialog::getDialogState() const
{
	// Do not add a default case so that if someone adds a new SipState they will get a warning here.
	// Therefore we define every state including the impossible ones.
	switch (getSipState()) {
	case SSNullState:
		return DialogState::dialogUndefined;
	case Starting:		// (pat) MOC or MOSMS or inboundHandover sent INVITE; MTC not used.
		return DialogState::dialogStarted;
	case Proceeding:		// (pat) MOC received Trying, Queued, BeingForwarded; MTC sent Trying
	case Connecting:		// (pat) MTC sent OK.
		// TODO: Is this correct for MTC Connecting?
		return DialogState::dialogProceeding;
	case Ringing:		// (pat) MOC received Ringing, notably not used for MTC sent Ringing, which is probably a bug of no import.
		return DialogState::dialogRinging;
	case Active:			// (pat) MOC received OK; MTC sent ACK
		return DialogState::dialogActive;

	case MODClearing:	// (pat) MOD sent BYE
	case MODCanceling:	// (pat) MOD sent a cancel, see forceSIPClearing.
	case MTDClearing:	// (pat) MTD received BYE.
	case MTDCanceling:	// (pat) received CANCEL
	case Canceled:		// (pat) received OK to CANCEL.
	case Cleared:		// (pat) MTD sent OK to BYE, or MTD internal error loss of FIFO, or MOSMS received OK, or MTSMS sent OK.
		return DialogState::dialogBye;

	case MOCBusy:			// (pat) MOC received Busy; MTC not used.
	case SSTimeout:
	case MODError:		// (pat) MOD sent a cancel, see forceSIPClearing.
	case SSFail:
		return DialogState::dialogFail;

	//case SipRegister:	// (pat) This SIPEngine is being used for registration, none of the other stuff applies.
	//case SipUnregister:	// (pat) This SIPEngine is being used for registration, none of the other stuff applies.
	case MOSMSSubmit:		// (pat) SMS message submitted, "MESSAGE" method.  Set but never used.
	case HandoverInbound:
	case HandoverOutbound:
		return DialogState::dialogUndefined;
	}
	devassert(0);
	return DialogState::dialogUndefined;
}


// Handle response to INVITE or MESSAGE.
// Only responses (>=200) to INVITE get an ACK.  Specifically, not MESSAGE.
void SipDialog::handleInviteResponse(int status,
	bool sendAck)		// TRUE if transaction is INVITE.  We used to use this for MESSAGE also, in which case it was false.
{
	LOG(DEBUG) <<LOGVAR(status) <<LOGVAR(sendAck);
	switch (status) {
		// class 1XX: Provisional messages
		case 100:	// Trying
		case 181:	// Call Is Being Forwarded
		case 182:	// Queued
		case 183:	// Session Progress FIXME we need to setup the sound channel (early media)
			dialogPushState(Proceeding,status);
			break;
		case 180:	// Ringing
			dialogPushState(Ringing,status);
			break;

		// class 2XX: Success
		case 200:	// OK
				// Save the response and update the state,
				// but the ACK doesn't happen until the call connects.
			dialogPushState(Active,status);
			break;

		// class 3xx: Redirection
		case 300:	// Multiple Choices
		case 301:	// Moved Permanently
		case 302:	// Moved Temporarily
		case 305:	// Use Proxy
		case 380:	// Alternative Service
			LOG(NOTICE) << "redirection not supported code " << status <<sbText();
			dialogPushState(SSFail,status, 'D');
			gReports.incr("OpenBTS.SIP.Failed.Remote.3xx");
			// TODO: What if it is not MOC?
			if (sendAck) MOCSendACK();
			break;
		// Anything 400 or above terminates the call, so we ACK.
		// FIXME -- It would be nice to save more information about the
		// specific failure cause.

		// class 4XX: Request failures
		case 405:	// Method Not Allowed
			// We must not ACK to "405 Method Not Allowed" or you could have an infinite loop.  Saw this with smqueue.
			dialogPushState(SSFail,status, 'D');
			gReports.incr("OpenBTS.SIP.Failed.Remote.4xx");
			break;

		case 400:	// Bad Request
		case 401:	// Unauthorized: Used only by registrars. Proxys should use proxy authorization 407
		case 402:	// Payment Required (Reserved for future use)
		case 403:	// Forbidden
		case 404:	// Not Found: User not found
		case 406:	// Not Acceptable
		case 407:	// Proxy Authentication Required
		case 408:	// Request Timeout: Couldn't find the user in time
		case 409:	// Conflict
		case 410:	// Gone: The user existed once, but is not available here any more.
		case 413:	// Request Entity Too Large
		case 414:	// Request-URI Too Long
		case 415:	// Unsupported Media Type
		case 416:	// Unsupported URI Scheme
		case 420:	// Bad Extension: Bad SIP Protocol Extension used, not understood by the server
		case 421:	// Extension Required
		case 422:	// Session Interval Too Small
		case 423:	// Interval Too Brief
		case 480:	// Temporarily Unavailable
		case 481:	// Call/Transaction Does Not Exist
		case 482:	// Loop Detected
		case 483:	// Too Many Hops
		case 484:	// Address Incomplete
		case 485:	// Ambiguous
			LOG(NOTICE) << "request failure code " << status <<sbText();
			dialogPushState(SSFail,status, 'D');
			gReports.incr("OpenBTS.SIP.Failed.Remote.4xx");
			if (sendAck) MOCSendACK();
			break;

		case 486:	// Busy Here
			LOG(NOTICE) << "remote end busy code " << status <<sbText();
			dialogPushState(MOCBusy,status,'D');
			// TODO: What if it is not MOC?
			if (sendAck) MOCSendACK();
			break;
		case 487:	// Request Terminated
		case 488:	// Not Acceptable Here
		case 491:	// Request Pending
		case 493:	// Undecipherable: Could not decrypt S/MIME body part
			LOG(NOTICE) << "request failure code " << status <<sbText();
			dialogPushState(SSFail,status,'D');
			gReports.incr("OpenBTS.SIP.Failed.Remote.4xx");
			if (sendAck) MOCSendACK();
			break;

		// class 5XX: Server failures
		case 500:	// Server Internal Error
		case 501:	// Not Implemented: The SIP request method is not implemented here
		case 502:	// Bad Gateway
		case 503:	// Service Unavailable
		case 504:	// Server Time-out
		case 505:	// Version Not Supported: The server does not support this version of the SIP protocol
		case 513:	// Message Too Large
			LOG(NOTICE) << "server failure code " << status <<sbText();
			dialogPushState(SSFail,status,'D');
			gReports.incr("OpenBTS.SIP.Failed.Remote.5xx");
			// TODO: What if it is not MOC?
			if (sendAck) MOCSendACK();
			break;

		// class 6XX: Global failures
		case 600:	// Busy Everywhere
		case 603:	// Decline
			dialogPushState(MOCBusy,status,'D');
			if (sendAck) MOCSendACK();
			break;
		case 604:	// Does Not Exist Anywhere
		case 606:	// Not Acceptable
			LOG(NOTICE) << "global failure code " << status <<sbText();
			dialogPushState(SSFail,status,'D');
			gReports.incr("OpenBTS.SIP.Failed.Remote.6xx");
			if (sendAck) MOCSendACK();
		default:
			LOG(NOTICE) << "unhandled status code " << status <<sbText();
			dialogPushState(SSFail,status,'D');
			gReports.incr("OpenBTS.SIP.Failed.Remote.xxx");
			if (sendAck) MOCSendACK();
	}
}

// Look for <tag>blah</tag> in xmlin and return "blah".
static string xmlFind(const char *xmlin, const char *tag)
{
	char tagbuf[56];
	assert(strlen(tag) < 50);
	sprintf(tagbuf,"<%s>",tag);
	const char *start = strstr(xmlin,tagbuf);
	if (!start) return string("");
	const char *result = start + strlen(tagbuf);
	sprintf(tagbuf,"</%s>",tag);
	const char *end = strstr(start,tagbuf);
	if (!start) return string("");
	return string(result,end-result);
}

// The incoming USSD BYE message could have a payload to be sent to the MS.
void SipDialog::handleUssdBye(SipMessage *msg)
{
	// There could be multiple BYE messages, hopefully all identical, but we only want to send one DialogMessage.
	if (getSipState() == Cleared) return;
	DialogUssdMessage *dmsg = new DialogUssdMessage(mTranId,DialogState::dialogBye,0);
	// Is it is ok for there to be no response string?
	// We have to send something to the MS so in that case return an empty string.
	if (msg->smGetMessageContentType().find("application/vnd.3gpp.ussd+xml") == string::npos) {
		LOG(INFO) << "UUSD response does not contain correct body type";
	} else {
		dmsg->dmMsgPayload = xmlFind(msg->smGetMessageBody().c_str(),"ussd-string");
		if (dmsg->dmMsgPayload == "") {
			// This is ok.
			LOG(INFO) << "Missing UUSD response does not contain correct body type";
		}
	}
	dialogQueueMessage(dmsg);
	if (dsPeer()->ipIsReliableTransport()) {
		dialogPushState(Cleared,0);
	} else {
		dialogPushState(MTDClearing,0);
		setTimerJ();
	}
}


// The SIPInterface sends this to us based on mCallId.
// We will process the message and possibly send replies or DialogMessages to the L3 State machine.
// Blah, this should be handled by Dialog sub-classes.
void SipDialog::dialogWriteDownlink(SipMessage *msg)
{
	LOG(DEBUG) <<"received SIP" /*<<LOGVAR2("SIP.state",sipState())*/ <<" msg:"<<msg->text() <<dialogText();
	sipStopTimers();
	ScopedLock lock(mDialogLock,__FILE__,__LINE__);

	unsigned code = msg->smGetCode();

	//if (code == 200) { saveResponse(msg); }
	//if (code >= 400) { mFailCode = code; }

	//SipDialog::msgState nextDialogState = sipMessage2DialogState(msg);

	switch (mDialogType) {
	case SIPDTRegister:
	case SIPDTUnregister:
		LOG(ERR) << "REGISTER transaction received unexpected message:"<<msg;
		break;
	case SIPDTMOUssd:
		LOG(DEBUG);
		if (code == 0 && msg->isBYE()) {	// It is a SIP Request.  Switch based on the method.
			// Grab any xml ussd response from the BYE message.
			handleUssdBye(msg);
		}
		goto otherwise;
	case SIPDTMOC: // This is a MOC transaction.
	case SIPDTMTC: // This is a MTC transaction.  Could be an inbound handover.
		LOG(DEBUG);
		otherwise:
		if (code == 0) {	// It is a SIP Request.  Switch based on the method.
			if (msg->isBYE()) {
				SipMTBye(msg);
			} else if (msg->isCANCEL()) {
				// This is an error since we have already passed the ACK stage, but lets cancel the dialog anyway.
				SipMTCancel(msg);
			} else {
				// Not expecting any others.  Must send 405 error.
				LOG(ALERT)<<"SIP Message ignored:"<<msg;	// TEMPORARY: Make this show up.
				LOG(WARNING)<<"SIP Message ignored:"<<msg;
				SipMessageReply oops(msg,405,string("Method Not Allowed"),this);
				sipWrite(&oops);
			}
		} else {
			// This should have matched a Transaction somewhere.
			// We cant send an error back for an unrecognized response or we get in an infinite loop.
			LOG(ALERT) << "SIP response not handled:"<<msg;
		}
		break;
	case SIPDTMOSMS:
	case SIPDTMTSMS:
		LOG(ERR) << "MESSAGE transaction received unexpected message:"<<msg;
		break;
	default:
		assert(0);
	}
	dialogChangeState(msg);
	delete msg;
}

// This is only called after the dialog has already been removed from the active dialogs,
// so we dont have to check the dialog state, all we have to check is there is nothing pointing to it
// that would cause a crash if genuinely deleted.
bool SipDialog::dgIsDeletable() const
{
	ScopedLock lock(mDialogLock,__FILE__,__LINE__);
	switch (mDialogType) {
		case SIPDTMOC:
		case SIPDTMTC:
		case SIPDTMOSMS:
		case SIPDTMTSMS:
		case SIPDTMOUssd:
			return gNewTransactionTable.ttIsDialogReleased(this->mTranId);
		// We never expire the dialog associated with REGISTER.
		case SIPDTRegister:
		case SIPDTUnregister:
		case SIPDTUndefined:
			return false;	// We never delete the Register dialog.
		default:
			assert(0);
	}
}

// Called periodicially to check for SIP timer expiration.
bool SipDialog::dialogPeriodicService()
{
	// Take care.  This is a potential deadlock if somone tries to add a locked SipDialog into the DialogMap,
	// because the kicker code locks the whole DialogMap against modification.
	ScopedLock lock(mDialogLock,__FILE__,__LINE__);
	// Now we use TransactionUsers for client transactions, so this code handles only server transactions.
	// The in-dialog server transactions are trivial - the transaction-layer simply resends the final
	// response each time the request is received.
	switch (mDialogType) {
		case SIPDTUndefined:
		case SIPDTRegister:
		case SIPDTUnregister:
			// FIXME: I dont think we delete these, ever.
			break;
		case SIPDTMTSMS:
		case SIPDTMTC:
			return mtPeriodicService();
			break;
		case SIPDTMOSMS:
		case SIPDTMOC:
		case SIPDTMOUssd:
			return moPeriodicService();
			break;
		//default: break;
	}
	return false;
}


const char *DialogState::msgStateString(DialogState::msgState dstate)
{
	switch (dstate) {
		case DialogState::dialogUndefined: return "undefined";
		case DialogState::dialogStarted: return "Started";
		case DialogState::dialogProceeding: return "Proceeding";
		case DialogState::dialogRinging: return "Ringing";
		case DialogState::dialogActive: return "Active";
		case DialogState::dialogBye: return "Bye";
		case DialogState::dialogFail: return "Fail";
		case DialogState::dialogDtmf: return "DTMF";
	};
	return "unknown_DialogState";
}

string SipDialog::dialogText(bool verbose) const
{
	std::ostringstream ss;
	ss << " SipDialog("<<LOGVARM(mTranId) ;
	ss << LOGVAR2("state",getDialogState()) <<LOGVARM(mPrevDialogState);
	//ss << LOGVAR2("fifo",mDownlinkFifo.size());
	SipBase::sbText(ss,verbose);
	// The C++ virtual inheritance is so broken we cant use it.  Gag me.
	switch (mDialogType) {
		case SIPDTMTC: case SIPDTMTSMS:
			ss << mttlText();
			break;
		case SIPDTMOC: case SIPDTMOSMS: case SIPDTMOUssd:
			ss << motlText();
			break;
		default: ss << "."; break;
	}
	ss <<")";
	return ss.str();
}

std::ostream& operator<<(std::ostream& os, const SipDialog*dg) {
	if (dg) os << dg->dialogText(); else os << "(null SipDialog)";
	return os;
}
std::ostream& operator<<(std::ostream& os, const SipDialog&dg) { os << dg.dialogText(); return os; }	// stupid language

std::ostream& operator<<(std::ostream& os, const DialogState::msgState dstate)
{
	os << DialogState::msgStateString(dstate);
	return os;
}

std::ostream& operator<<(std::ostream& os, const DialogMessage*dmsg)
{
	if (dmsg) {
		os <<"DialogMessage("<<LOGVAR2("MsgState",DialogState::msgStateString(dmsg->mMsgState)) <<LOGVAR2("StatusCode",dmsg->mSipStatusCode)<<")";
	} else {
		os << "(null DialogMessage)";
	}
	return os;
}

std::ostream& operator<<(std::ostream& os, const DialogMessage&dmsg) { os << &dmsg; return os; }	// stupid language


};	// namespace
