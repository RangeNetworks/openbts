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

#define LOG_GROUP LogGroup::Control		// Can set Log.Level.Control for debugging

#include <set>
#include <algorithm> // for std::remove
#include "L3TranEntry.h"
#include <GSMLogicalChannel.h>
#include "ControlCommon.h"
#include "L3MobilityManagement.h"
#include "L3CallControl.h"
#include "L3SMSControl.h"
#include "L3MMLayer.h"
#include "L3SupServ.h"
#include <Logger.h>
#include <Interthread.h>
#include <Timeval.h>
#include <Globals.h>


#include <GSMConfig.h>
#include <GSMTransfer.h>
#include "ControlCommon.h"
//#include <GSML3CommonElements.h>
//#include <GSML3MMElements.h>
//#include <GSML3CCElements.h>
#include <GSML3Message.h>		// Doesnt this poor L3Message get lonely?  When apparently there are multiple L3MMMessages and L3CCMessages?
#include <GSML3MMMessages.h>
#include <GSML3CCMessages.h>
//#include <SIPDialog.h>
#include <SIPExport.h>
#include <Regexp.h>
#include "RRLPServer.h"
using namespace GSM;



// Note: GSM 4.08 4.1.2.3 has MM States on Network Side.

namespace Control {
using namespace SIP;
using namespace GSM;

static const int testWelcomeMessage = 1;


void NewCMServiceResponder(const L3CMServiceRequest* cmsrq, MMContext* mmchan)
{
	assert(cmsrq);
	assert(mmchan);
	LOG(INFO) << *cmsrq;
	//TranEntry *tran;
	// The transaction may or may not be cleared,
	// depending on the assignment type.
	CMServiceTypeCode serviceType = cmsrq->serviceType().type();
	switch (serviceType) {
		case L3CMServiceType::MobileOriginatedCall:
			gReports.incr("OpenBTS.GSM.MM.CMServiceRequest.MOC");
			startMOC(cmsrq,mmchan,serviceType);
			break;
		case L3CMServiceType::ShortMessage:
			gReports.incr("OpenBTS.GSM.MM.CMServiceRequest.MOSMS");
			startMOSMS(cmsrq,mmchan);
			break;
		case L3CMServiceType::SupplementaryService:
			startMOSSD(cmsrq,mmchan);
			break;
		default:
			gReports.incr("OpenBTS.GSM.MM.CMServiceRequest.Unhandled");
			LOG(NOTICE) << "service not supported for " << *cmsrq;
			mmchan->l3sendm(L3CMServiceReject(L3RejectCause::Service_Option_Not_Supported));
			//mmchan->l3sendm(L3ChannelRelease(L3RRCause::Unspecified));
			return;
	}
}

// For MTC we paged the MS, it RACHed in, was given a channel with an ImmediateAssignment,
// without knowing the MS identity.  The MS then sends us this message.
// The purpose of this function is to identify the MS so we can associate the
// radio channel with the MMUser.
void NewPagingResponseHandler(const L3PagingResponse* resp, MMContext* mmchan)
{
	assert(resp);
	assert(mmchan);
	LOG(INFO) << *resp;

	// Nowadays, we dont page unless we know both the tmsi and the imsi of the MS, so just look it up.
	L3MobileIdentity mobileId = resp->mobileID();
	if (! gMMLayer.mmPageReceived(mmchan,mobileId)) {

		LOG(WARNING) << "Paging Reponse with no Mobility Management record (probably timed out) for " << mobileId;
		mmchan->l3sendm(L3ChannelRelease(L3RRCause::Call_Already_Cleared));
		return;		// There is nothing more we can do about this because we dont know who it is.
	}
}

// The BLU Deco Mini handset rejects the first SMS with a "protocol error unspecified" after an LUR procedure
// regardless if it is delivered immediately on the same channel, paged later, or even if you wait 10 seconds.
// Must wait 30 seconds before it will accept a new SMS.
// Evidently we need to watch the return state of the welcome message so we can resend it if necessary.
// But this problem should be hoisted into sipauthserve.
static void sendWelcomeMessage(MMSharedData *mmsd, const char* messageName, const char* shortCodeName, const FullMobileId &msid,
	L3LogicalChannel* DCCH)
{
	LOG(DEBUG);
	if (mmsd->store.getWelcomeSent()) { return; }

	// (pat) TODO: We should store the authorization state of the welcome message that was sent so that when there is an
	// authorization state change (ie, from unauthorized to authorized) we can send a new welcome message.
	// But this should all be moved into sipauthserve anyway.
	string stmp = gConfig.getStr("Control.LUR.RegistrationMessageFrequency");
	if (stmp == "PLMN") {
		// We only send the registration message if it is an imsi attach.
		// If it is a normal updating we assume a welcome message was sent by a different BTS, or possibly
		// earlier by us and the TMSI_TABLE database was lost.
		// If it is a periodic updating we assume a welcome message was sent by us but we lost the tmsi database somehow.
		if (! mmsd->isImsiAttach()) {
			mmsd->store.setWelcomeSent(2);	// welcome message sent by someone else
			LOG(DEBUG);
			return;
		}
	} else if (stmp == "NORMAL") {
		// We send the registration message the first time this BTS sees this MS.
		// If it is periodic updating, then we assume that we have seen the MS previously but our TMSI_TABLE database was lost.
		if (! mmsd->isInitialAttach()) {
			mmsd->store.setWelcomeSent(2);	// welcome message sent by this BTS previously.
			LOG(DEBUG);
			return;
		}
	} else {
		// This is the stmp == 'FIRST' option.
		// We send the message if the WELCOME_SENT field is 0, regardless of the status reported by the MS.
	}
	LOG(DEBUG);

	if (!gConfig.defines(messageName) || !gConfig.defines(shortCodeName)) return;
	string message = gConfig.getStr(messageName);
	string shortCode = gConfig.getStr(shortCodeName);
	if (!message.length() || !shortCode.length()) return;
	LOG(INFO) << "sending " << messageName << " message to handset";
	message += string(" IMSI:") + msid.mImsi;
	// (pat) We use the short code as the originator calling number so the user can hit reply.
	Control::TranEntry *tran = Control::TranEntry::newMTSMS(
						NULL,					// No SipDialog
						msid,					// MS we are sending SMS to.
						GSM::L3CallingPartyBCDNumber(shortCode.c_str()),
						message,				// message body
						string("text/plain"));	// message content type
	if (1) {
		// This line starts the SMS immediately after the Mobility Management procedure finishes on the same channel;
		// it works on the Blackberry but does not work on the BLU mini, but nothing works on that.
		DCCH->chanGetContext(false)->startSMSTran(tran);
	} else {
		// This causes the BTS to page the MS for the SMS message after the MM procedure releases the channel.
		// Some handsets will not respond to a page if sent certain LU reject codes, so this does not work
		// to send the reject message.
		Control::gMMLayer.mmAddMT(tran);
	}
	mmsd->store.setWelcomeSent(1);	// welcome message sent by us.  TODO: We should not set this until we have confirmation of delivery.
	return;
}

// The L3IdentifyMachine is invoked for SMS and USSD.  It is not used during the Location Update procedure.
MachineStatus L3IdentifyMachine::machineRunState(int state, const GSM::L3Message *l3msg, const SIP::DialogMessage *sipmsg)
{
	PROCLOG2(DEBUG,state)<<LOGVAR(l3msg)<<LOGVAR(sipmsg)<<LOGVAR2("imsi",tran()->subscriber());
	switch (state) {
		// This is the start state.  It may return immediately if the MS is already identified.
		case stateStart: {
			// Have an imsi already?
			if (mMobileID.type()==IMSIType) {
				string imsi(mMobileID.digits());
				tran()->setSubscriberImsi(imsi,false);
				*mResultPtr = gTMSITable.tmsiTabCheckAuthorization(imsi);
				return MachineStatusPopMachine;
			}

			// If we got a TMSI, find the IMSI.
			if (mMobileID.type()==TMSIType) {
				unsigned authorized;
				string imsi = gTMSITable.tmsiTabGetIMSI(mMobileID.TMSI(),&authorized);
				LOG(DEBUG) <<"lookup"<<LOGVAR(mMobileID.TMSI()) <<LOGVAR(imsi);
				if (imsi.size()) {
					// TODO: We need an option to re-authenticate this handset, but for now,
					// just use the authorization value saved in the TMSITable from the most recent LUR on this BTS.
					// Note that the handset may have been subsequently unauthorized on a different BTS and
					// we would not know about it.
					// TODO We could check the date of our cached authorization and force a re-authorization if it is expired.
					tran()->setSubscriberImsi(imsi,false);
					*mResultPtr = authorized;
					return MachineStatusPopMachine;
				}
			}


			// Still no IMSI?  Ask for one.
			// TODO: We should ask the SIP Registrar.
			// (pat) This is not possible if the MS is compliant (unless the TMSI table has been lost) -
			// the MS should have done a LocationUpdate first, which provides us with the IMSI.
			PROCLOG(NOTICE) << "No IMSI or valid TMSI.  Reqesting IMSI.";
			timerStart(T3270,T3270ms,TimerAbortChan);
			channel()->l3sendm(L3IdentityRequest(IMSIType));
			return MachineStatusOK;
		}


		// TODO: This should be moved to an MM Identify procedure run before starting the MOC.
		case L3CASE_MM(IdentityResponse): {
			timerStop(T3270);
			const L3IdentityResponse *resp = dynamic_cast<typeof(resp)>(l3msg);
			const L3MobileIdentity &mobileID = resp->mobileID();	// Do not need a copy operation.
			if (mobileID.type()==IMSIType) {
				string imsi = string(mobileID.digits());
				tran()->setSubscriberImsi(imsi,false);
				*mResultPtr = gTMSITable.tmsiTabCheckAuthorization(imsi);

			} else {
				// FIXME -- This is quick-and-dirty, not following GSM 04.08 5.
				PROCLOG(WARNING) << "Requested IMSI but got:"<<resp;
				*mResultPtr = false;
			}
			return MachineStatusPopMachine;
		}
		default:
			// TODO: Should we handle other messages here?
			return unexpectedState(state,l3msg);
	}
}


string LUBase::getImsi() const { return tran()->subscriberIMSI(); }
const char * LUBase::getImsiCh() const { return getImsi().c_str(); }
const string LUBase::getImsiName() const { return string("IMSI") + getImsi(); }
FullMobileId &LUBase::subscriber() const { return tran()->subscriber(); }
MMSharedData *LUBase::ludata() const
{
	if (!tran()->mMMData) { tran()->mMMData = new MMSharedData; }
	return tran()->mMMData;
}


// TODO: Reject cause should be determined in a more central location, probably sipauthserve.
// We may want different reject codes based on the IMSI or MSISDN of the MS, or on the CM service being requested (CC, SMS, USSD, GPRS),
// although this code here is used only by LUR.
static MMRejectCause getRejectCause(unsigned sipCode)
{
	MMRejectCause rejectCause;
	unsigned utmp;
	switch (sipCode) {
		case 400:	// This value is used in the SIP code for unrecoverable errors in a SIP message from the Registrar.
			rejectCause = L3RejectCause::Network_Failure;
			break;
		case 401: {	// SIP 401 "Unauthorized"
			// The sip nomenclature for 401 and 404 are exactly reversed:
			// This sip code is "Unauthorized" but what it really means is the Registrar
			// failed the IMSI without a challenge, ie, the MS was not found in the database.
			utmp = gConfig.getNum("Control.LUR.UnprovisionedRejectCause");
			rejectCause = (MMRejectCause) utmp;
			break;
		}
		case 403: {	// SIP 403 "Forbidden"
			rejectCause = L3RejectCause::Location_Area_Not_Allowed;
			break;
		}
		case 404: {	// SIP 404 "Not Found"
			// The sip nomenclature for this code is "Not Found" but it really means failed authorization.
			// (pat) TODO: The reject cause may want to be different for home and roaming subscribers,
			// and may want to depend on the IMSI or MSISDN, and we may want allow or disallow on the same criteria
			// in which case it is already too late here; but the appropriate code should
			// be determined at the Registrar level, not in the BTS, so this is not the place to fix it.
			utmp = gConfig.getNum("Control.LUR.404RejectCause");
			rejectCause = (MMRejectCause) utmp;
			break;
		}
		case 424: {	// SIP 424 "Bad Location Information"
			rejectCause = L3RejectCause::Roaming_Not_Allowed_In_LA;
			break;
		}
		case 504: {	// SIP 504 "Servier Time-out"
			rejectCause = L3RejectCause::Congestion;
			break;
		}
		case 603: { // SIP 603 "Decline"
			rejectCause = L3RejectCause::IMSI_Unknown_In_VLR;
			break;
		}
		case 604: { // SIP 604 "Does Not Exist Anywhere"
			rejectCause = L3RejectCause::IMSI_Unknown_In_HLR;
			break;
		}
		default:
			LOG(NOTICE) << "REGISTER unexpected response from Registrar" <<LOGVAR(sipCode);
			rejectCause = L3RejectCause::Network_Failure;
			break;
	}
	LOG(INFO) << "SIP term info sip code: " << sipCode << " rejectCause: " << rejectCause;
	LOG(DEBUG) <<LOGVAR(sipCode)<<LOGVAR(rejectCause);
	return rejectCause;	// TODO: We should check what the user entered makes sense.
}

static void checkForConfigChanges()
{
	// TODO: we could save these in the TMSI table properties so that we dont clear the auth cache when
	// the BTS is rebooted.
	static string saveOpenRegistrationPat, saveRejectPat;
	string openRegistrationPat(gConfig.getStr("Control.LUR.OpenRegistration"));
	string rejectPat(gConfig.getStr("Control.LUR.OpenRegistration.Reject"));
	if (saveOpenRegistrationPat != openRegistrationPat || saveRejectPat != rejectPat) {
		saveOpenRegistrationPat = openRegistrationPat;
		saveRejectPat != rejectPat;
		gTMSITable.tmsiTabClearAuthCache();
	}
}


bool LUBase::openRegistration() const
{
	bool allow = false;	// Will open registration apply to this MS?
	string openRegistrationPat(gConfig.getStr("Control.LUR.OpenRegistration"));
	if (openRegistrationPat.size() && gConfig.isValidValue("Control.LUR.OpenRegistration", openRegistrationPat)) {
		// (pat) Removed this - default is in the configuration database, and an empty message allowed.
		//if (!gConfig.defines("Control.LUR.OpenRegistration.Message")) {
		//	gConfig.set("Control.LUR.OpenRegistration.Message","Welcome to the test network.  Your IMSI is ");
		//}
		Regexp rxp(openRegistrationPat.c_str());
		string imsi(getImsi());
		allow = rxp.match(imsi.c_str());
		LOG(DEBUG) << "Checking Open Registration Open Registration"<<LOGVAR(imsi)<<LOGVAR(allow)<<LOGVAR2("pattern",openRegistrationPat);
		if (allow) {
			string rejectPat(gConfig.getStr("Control.LUR.OpenRegistration.Reject"));
			if (rejectPat.size() && gConfig.isValidValue("Control.LUR.OpenRegistration.Reject", rejectPat)) {
				Regexp rxpReject(rejectPat.c_str());
				if (rxpReject.match(getImsiCh())) {
					LOG(DEBUG) << "Open Registration denied by match of reject pattern"<<LOGVAR(imsi);
					allow = false;
				}
			}
		}
	} else {
		LOG(DEBUG) << "Open Registration not enabled.";
	}
	return allow;
}

bool LUBase::failOpen() const
{
	string failmode = gConfig.getStr("Control.LUR.FailMode");
	if (failmode == "FAIL") { return false; }
	if (failmode == "OPEN") { return openRegistration(); }
	return true;
}


// ====== State Machine LUStart =====
// Procedure hierarchy:
// LUStart
//	call L3RegisterMachine
//	goto LUAuthentication
//		call L3RegisterMachine
//		if fail && tmsiProvisional goto LUStart
// 		goto LUFinish.

// (pat) Some really old comments where I was thinking out loud.  These do not describe the existing code.
//	For each service: Attach, periodic LUR, CC, SMS, GPRS, we should support these options:
//		- accept unconditionally without registration.  For CC we must query the IMSI.  For others we dont even have to do that.
//		- accept cached (ie, previously authenticated) TMSI or IMSI (current code does this for CC).
//		- accept cached (ie, previously authenticated) IMSI or TMSI authenticated with cached nonce/sres.
//		- REGISTER, noting that takes any further decisions are out of our hands.
//		- REGISTER, OpenRegistration backup: On REGISTER failure, accept, optionally with cached nonce/sres authentication.
// However, all such logic should be in sipauthserve, not the BTS.
// Update: The best way to do this would be with a javascript interterpter that controls the registration process
// with variables for IMSI, service, registration-state, and the various welcome messages could be implemented by calls.
// See tiny-js although that does not have REs.

// This is the start state for the LUStart sub state machine of the Location Updating Procedure:
// Resolve an IMSI and see if there's a pre-existing IMSI-TMSI mapping.
MachineStatus LUStart::stateRecvLocationUpdatingRequest(const GSM::L3LocationUpdatingRequest *lur)
{
	checkForConfigChanges();

	timerStart(TMMCancel,12000,TimerAbortChan);
	// Save what we need from the LocationUpdatingRequest message.
	ludata()->mLUMobileId = lur->mobileID();	// This is a copy operation.
	ludata()->mLULAI = lur->LAI();			// This is a copy operation too.
	// (pat) Documentation for IMSI Attach is in 24.008 4.4.3.
	// The documentation is confusing, but I observe that when the MS is first powered on, it sends a
	// "Normal Location Updating" if it was previously IMSI attached (ie, successful LUR) in the same PLMN.
	ludata()->mLUType = lur->getLocationUpdatingType();

	// The location updating request gets mapped to a SIP
	// registration with the SIP registrar.

	// If the handset is allowed to register it may receive a TMSI reassignment.
	gReports.incr("OpenBTS.GSM.MM.LUR.Start");

	switch (ludata()->mLUMobileId.type()) {
		case GSM::IMSIType: {
			ludata()->mFullQuery = true;
			string imsi = string(ludata()->mLUMobileId.digits());
			// TODO: We should notify the MM layer.
			//gMMLayer.mmBlockImsi(imsi);
			tran()->setSubscriberImsi(imsi,false);	// We will need this for authorization below.
			// If the MS sent an IMSI but the TMSI is already in the database, most likely we just did not send the TMSI assignment.
			// but it could also be a tmsi collision.
			uint32_t tmsi = gTMSITable.tmsiTabGetTMSI(imsi,false); // returns 0 if IMSI not in database.
			if (tmsi) {
				ludata()->setTmsi(tmsi,tmsiNotAssigned);
				//gMMLayer.mmBlockTmsi(tmsi);
			} else {
				assert(ludata()->getTmsiStatus() == tmsiNone);
			}
			return machineRunState(stateHaveImsi);
		}
		case GSM::TMSIType: {
			uint32_t tmsi = ludata()->mLUMobileId.TMSI();
			ludata()->mOldTmsi = tmsi;
			// Look in the TMSI table to see if it's one we assigned.
			bool sameLAI = ludata()->mLULAI == gBTS.LAI();
			string imsi;
			if (sameLAI) imsi = gTMSITable.tmsiTabGetIMSI(tmsi,NULL);
			if (imsi.size()) {
				// There is an TMSI/IMSI pair already in the TMSI table corresponding to this TMSI,
				// but we dont know if this is the same MS yet.  We will try to authenticate using the stored IMSI,
				// and if that fails, we will query for the IMSI and try again.
				tran()->setSubscriberImsi(imsi,false);	// We will need this for authorization below.
				ludata()->setTmsi(tmsi,tmsiProvisional);	// We (may have) assigned this tmsi sometime in the past.
				//gMMLayer.mmBlockTmsi(tmsi);
				//gMMLayer.mmBlockImsi(imsi);
				LOG(DEBUG) << "resolving mobile ID (table): " << ludata()->mLUMobileId;
				return machineRunState(stateHaveImsi);
			} else {
				// Unrecognized TMSI; Query for IMSI
				// We leave the TMSI state at tmsiNone and save the unrecognized tmsi only in mOldTmsi.
				ludata()->mFullQuery = true;
				return sendQuery(IMSIType);
			}
		}
		case GSM::IMEIType:
			// (pat) The phone was not supposed to send an IMEI in the LUR message,
			// but lets go ahead and accept it.  So we need to query for the imsi:
			ludata()->store.setImei(ludata()->mLUMobileId.digits());
			return sendQuery(IMSIType);
		default:
			LOG(ERR) << "Unexpected MobileIdentity type in LocationUpdateRequest:"<<lur;
			// But who cares?  Lets try sending an identity request for what we want.
			// If it fails that, then we will reject it.
			ludata()->mFullQuery = true;
			return sendQuery(IMSIType);
	}
}

// Send a query.  Only send each query once.
MachineStatus LUStart::sendQuery(MobileIDType qtype)
{
	assert(qtype == IMSIType || qtype == IMEIType);
	ludata()->mQueryType = qtype;
	timerStart(T3270,12000,TimerAbortChan);
	channel()->l3sendm(GSM::L3IdentityRequest(qtype));
	return MachineStatusOK;
}

// Receive the identity response which may be IMSI or IMEI
MachineStatus LUStart::stateRecvIdentityResponse(const GSM::L3IdentityResponse *resp)
{
	//const GSM::L3IdentityResponse *resp = dynamic_cast<typeof(resp)>(l3msg);
	LOG(INFO) << *resp;
	MobileIDType idtype = resp->mobileID().type();
	// Meaningful IdentityResponse?

	// Store the result, even if it was not what we asked for.
	if (idtype == IMSIType) {
		string imsi = string(resp->mobileID().digits());

		{
			// Check for perfidy on the part of the MS.  We're checking that it did not send two different IMSIs in the same attempt,
			// something that will probably never happen.
			string prevImsi = getImsi();
			if (prevImsi.size() && prevImsi != imsi) {
				LOG(ERR) << "MS returned two different IMSIs";
				MMRejectCause failCause = L3RejectCause::Invalid_Mandatory_Information;
				// There is no ludata()->store yet so just set it directly:
				gTMSITable.tmsiTabSetRejected(imsi,(int)failCause);
				// I dont know what the cause should be here, but if this ever happens, we dont care.
				channel()->l3sendm(L3LocationUpdatingReject(failCause));
				LOG(INFO) << "SIP term info closeChannel called in stateRecvIdentityResponse";
				return closeChannel(L3RRCause::Normal_Event,L3_RELEASE_REQUEST,TermCause::Local(failCause));
			}
		}

		tran()->setSubscriberImsi(imsi,false);
		//gMMLayer.mmBlockImsi(imsi);

		// If this is the second attempt, we could check if this IMSI matches what we had on record,
		// and if so just reject immediately, but this doesn't happen often and I'm just going to let it proceed again
		// with the whole registration process.
		LOG(DEBUG) <<LOGVAR(imsi) <<LOGVAR(ludata()->mSecondAttempt) <<LOGVAR(ludata()->mPrevRegisterAttemptImsi) <<LOGVAR(ludata()->getTmsiStatus());
		if (ludata()->mSecondAttempt == 0) {
			// The MS sent a TMSI that was unrecognized, so we queried for the IMSI.
			assert(ludata()->getTmsiStatus() == tmsiNone);
		} else {
			// The second attempt means that registration by TMSI failed, so we queried for the imsi.
			// If the IMSI matches what is in the TMSI table, it is a genuine failure.
			devassert(ludata()->mPrevRegisterAttemptImsi.size());
			if (ludata()->mPrevRegisterAttemptImsi == imsi) {
				// It was not a TMSI collision; this subscriber is really unauthorized.
				// The register procedure already called regSetFail.
				devassert(ludata()->mRegistrationResult.isValid());
				return callMachStart(new LUFinish(tran()));
			}
			// We already assigned the new imsi above.  We just fall through to try another registration.
			assert(ludata()->getTmsiStatus() == tmsiFailed);
		}
	} else if (idtype == IMEIType) {
		// We do not check whether the IMEI matches what we may have stored already because
		// we dont care if the user has switched their SIM card to a new handset.
		ludata()->store.setImei(string(resp->mobileID().digits()));
	} else {
		LOG(WARNING) << "MS Identity Response unexpected type: " << idtype;
		return MachineStatusOK;	// Just ignore it.  T3270 is still running.  Maybe it will return what we asked for later.
	}

	if (ludata()->mQueryType == NoIDType) {
		// This was an unsoliticed or duplicate IdentityResponse.
		return MachineStatusOK;
	}

	if (ludata()->mQueryType == idtype) {	// success
		timerStop(T3270);
		ludata()->mQueryType = NoIDType;
		// Go to the next state.
		timerStart(TMMCancel,12000,TimerAbortChan);
		if (idtype == IMSIType) {
			return machineRunState(stateHaveImsi);
		} else {
			return machineRunState(stateHaveIds);
		}
	} else {
		// The MS goofed.
		LOG(WARNING) << "MS Identity Response for "<<ludata()->mQueryType<<" returned "<<idtype<<" instead";
		return MachineStatusOK;	// Keep going, maybe it will return what we want before T3270 runs out.
	}
}


MachineStatus LUStart::machineRunState(int state, const GSM::L3Message* l3msg, const SIP::DialogMessage *sipmsg)
{
	timerStart(TMMCancel,12000,TimerAbortChan);
	switch (state) {
	case L3CASE_MM(LocationUpdatingRequest): {	// This is the start state for the first attempt.
		const GSM::L3LocationUpdatingRequest *lur = dynamic_cast<typeof(lur)>(l3msg);
		return stateRecvLocationUpdatingRequest(lur);
	}

	// This is the start state for the second attempt.
	case stateSecondAttempt: {
		// The second attempt is initiated from LUAuthentication if registration by tmsi fails.
		ludata()->mFullQuery = true;
		return sendQuery(IMSIType);
	}

	case L3CASE_MM(IdentityResponse): {
		const GSM::L3IdentityResponse *resp = dynamic_cast<typeof(resp)>(l3msg);
		return stateRecvIdentityResponse(resp);
	}

	case stateHaveImsi:
	{
		if (ludata()->mFullQuery && gConfig.defines("Control.LUR.QueryIMEI") && ludata()->store.getImei().size() == 0) { return sendQuery(IMEIType); }
		return machineRunState(stateHaveIds);
	}

	case stateHaveIds:
	{
		// We have the IMSI and IMEI if needed.  Proceed with authorization.
		GPRS::GPRSNotifyGsmActivity(this->getImsiCh());

		// OpenBTS version 3 generated a TMSI here for every new phone we see, even if we don't actually assign it.

		// (pat) Start of Authorization Procedure.
		// TODO: This needs to pass a message to SR and wait for a message back.


		// What if a TMSI comes in and then the Registrar does not challenge it?
		// We could get the phones mixed up.
		// If the previous authorization has not expired:
		// If it was previously unauthorized, just reject it without contacting the Registrar.
		// If there was a challenge, we could accept immediately, or re-run the previous challenge,
		// or preferably, the Registrar would return a string of challenge/response pairs so we can keep using them.
		TmsiTableStore *store = &ludata()->store;
		// On the second attempt we need to do a real authentication via registration, not just re-run cached authentication.
		if (! ludata()->mSecondAttempt && gTMSITable.tmsiTabGetStore(getImsi(),store)) {
			// Is the cached authorization still valid?
			int authExpiry = store->getAuthExpiry();
			if (authExpiry && time(NULL) <= authExpiry) {
				if (! store->isAuthorized()) {
					// Not authorized.
					ludata()->mRegistrationResult.regSetFail(0,(MMRejectCause)store->getRejectCode());
					return callMachStart(new LUFinish(tran()));
				} else {
#if CACHE_AUTH
					// Handset was authorized.
					// We do not use the authorization cache if the handset is authorized because we
					// need to inform sipauthserve of the whereabouts of the handset so it can update the
					// database used by asterisk for MTC.
					ludata()->mUsingCachedAuthentication = true;
					//if (store->rand.size()) {
					//	ludata()->mRegistrationResult.regSetChallenge(0,store->rand);
					//	return callMachStart(new LUAuthentication(tran()));
					//} else {
					//	ludata()->mRegistrationResult.regSetSuccess();
					//	return callMachStart(new LUFinish(tran()));
					//}
#endif
				}
			}
		}

		// The TranEntry already has the correct SipEngine.
		// DCCH is available in tran()
		string emptySRES;
		ludata()->mPrevRegisterAttemptImsi = getImsi();
		return machPush(new L3RegisterMachine(tran(),SIPDTRegister,
							emptySRES, &ludata()->mRegistrationResult),
						stateRegister1Response);
	}

	case stateRegister1Response:
	{
		timerStart(TMMCancel,12000,TimerAbortChan);
		// Did we get a RAND for challenge-response?
		LOG(DEBUG)<<LOGVAR2("IMSI",getImsi())<<ludata()->text();
		if (ludata()->mRegistrationResult.mRegistrationStatus == RegistrationChallenge) {
			return callMachStart(new LUAuthentication(tran()));
		} else {
			return callMachStart(new LUFinish(tran()));
		}
#if 0
		if (ludata()->mRegistrationResult.isFailure()) {
		}
		//if (ludata()->mRAND.length() == 0)
		if (ludata()->mRegistrationResult.mRegistrationStatus != RegistrationChallenge) {
			// If the RAND is not provided, no challenge needed.
			// The phone may be authorized or not, but LUFinish handles both cases.
			return callMachStart(new LUFinish(tran()));
		}
#endif
	}
	default:
		return unexpectedState(state,l3msg);
	}	// switch
}

// ====== State Machine LUAuthentication =====

MachineStatus LUAuthentication::machineRunState(int state, const GSM::L3Message* l3msg, const SIP::DialogMessage *sipmsg)
{
	switch (state) {
		case stateStart: {
			gReports.incr("OpenBTS.GSM.MM.Authenticate.Request");
			// Get the mobile's SRES.
			LOG(INFO) << "sending " << ludata()->mRegistrationResult.text() << " to mobile";
			uint64_t uRAND;
			uint64_t lRAND;
			string rand = ludata()->mRegistrationResult.mRand;	// mRAND;
			rand = rand.substr(0,rand.find('.'));
			if (gConfig.getStr("SIP.Realm").length() > 0) {
				rand.erase(std::remove(rand.begin(), rand.end(), '-'), rand.end());
			}
			if (rand.size() != 32) {
				LOG(ALERT) << "Invalid RAND challenge returned by Registrar (RAND length=" <<rand.size() <<")";
				// (pat) LUFinish may still permit services depending on failOpen().
				ludata()->mRegistrationResult.regSetError();
				//channel()->l3sendm(L3LocationUpdatingReject(L3RejectCause::Service_Option_Temporarily_Out_Of_Order));
				//return closeChannel(L3RRCause::Normal_Event,RELEASE);
				return callMachStart(new LUFinish(tran()));
			}

			// TODO: This needs to be message based.
			Utils::stringToUint(rand, &uRAND, &lRAND);
			// Sending authenticaion request moved to LUAuthentication::stateStart
			timerStart(T3260,12000,TimerAbortChan);
			channel()->l3sendm(GSM::L3AuthenticationRequest(0,GSM::L3RAND(uRAND,lRAND)));
			return MachineStatusOK;
		}

		// The MS returns SRES in response to an authentication request with a RAND, and here we send a second
		// registration request back to the Registrar to see if the SRES is correct.
		case L3CASE_MM(AuthenticationResponse): {
			timerStop(T3260);
			timerStart(TMMCancel,12000,TimerAbortChan);	// TODO: How long should we wait for authentication?
			const GSM::L3AuthenticationResponse*resp = dynamic_cast<typeof(resp)>(l3msg);
			LOG(INFO) << *resp;
			uint32_t mobileSRES = resp->SRES().value();
			// verify the SRES that was sent to use by the MS.
			//ostringstream os;
			//os << hex << mobileSRES;
			//string SRESstr = os.str();
#if CACHE_AUTH
			if (ludata()->mUsingCachedAuthentication) {
				if (mobileSRES == ludata()->store.SRES) {
					ludata()->mRegistrationResult.regSetSuccess();
				} else {
					ludata()->mRegistrationResult.regSetFail(0,store->rejectCode);
				}
			} else
#endif
			{
				string SRESstr = format("%08x",mobileSRES);
				return machPush(new L3RegisterMachine(tran(),SIPDTRegister,
														SRESstr, &ludata()->mRegistrationResult),
								stateRegister2Response);
			}
		}

		case stateRegister2Response: {
			// The TMSI table is updated as follows:
			// on success, only in this case;
			// on failure, by LUFinish::stateSendLUResponse(), which is called in other places too.
			LOG(DEBUG) <<LOGVAR(ludata()->getTmsi()) <<LOGVAR(ludata()->getTmsiStatus()) <<LOGVAR(tran()->subscriberIMSI());
			timerStop(TMMCancel);
			switch (ludata()->mRegistrationResult.mRegistrationStatus) {
				case RegistrationUninitialized:
				default:
					devassert(0);
					// Fall Through
				case RegistrationError:
					//return callMachStart(new LUNetworkFailure(tran()));
					return callMachStart(new LUFinish(tran()));
				case RegistrationFail:	// In which case the mSipCode tells why.
					if (ludata()->mSecondAttempt == 0 && ludata()->getTmsiStatus() == tmsiProvisional) {
						//mmUnblockImsi(getImsi());
						// Registration by TMSI failed.  Try again using an IMSI.  To do that we will start authentication over from scratch.
						// Delete both the stored tmsi and the imsi stored in the transaction.
						ludata()->setTmsi(0,tmsiFailed);
						tran()->setSubscriberImsi(string(""),false);	// This IMSI was not authorized and may not be the IMSI for this TMSI.
						ludata()->mSecondAttempt = true;	// Start second attempt.
						// Start over and this time query for the IMSI and try again.
						return callMachStart(new LUStart(tran()),LUStart::stateSecondAttempt);
					} else {
						// We dont need to update the TmsiStatus because we are finished.
						// LUFinish will check open-registration, send a reject message if we really failed.
						return callMachStart(new LUFinish(tran()));
					}
				case RegistrationChallenge:
					// This should not happen.
					LOG(ERR) << "Registrar error: second registration includes challenge.";
					// What to do?
					ludata()->mRegistrationResult.regSetError();
					return callMachStart(new LUFinish(tran()));
				case RegistrationSuccess:
					// Authorization success: Move on.
					break;
			}
#if 0
			if (ludata()->mRegistrationResult.isNetworkFailure()) {
				return callMachStart(new LUNetworkFailure(tran()));
			} else if (ludata()->mRegistrationResult == RegistrationFail) {	// really failed.
				if (ludata()->mSecondAttempt == 0 && ludata()->getTmsiStatus() == tmsiProvisional) {
					// Registration by TMSI failed.  Try again using an IMSI.  To do that we will start authentication over from scratch.
					// Delete both the stored tmsi and the imsi stored in the transaction.
					ludata()->setTmsi(0,tmsiFailed);
					tran()->setSubscriberImsi(string(""),false);	// This IMSI was not authorized and may not be the IMSI for this TMSI.
					ludata()->mSecondAttempt = true;	// Start second attempt.
					// Start over and this time query for the IMSI and try again.
					return callMachStart(new LUStart(tran()));
				} else {
					// We dont need to update the TmsiStatus because we are finished.
					// LUFinish will check open-registration, send a reject message if we really failed.
					return callMachStart(new LUFinish(tran()));
				}
			} else
#endif
			{
				// Query for classmark?
				// (pat) We need to do this if the IMEI changed also, because a new handset may have different capabilities.
				// Instead of checking IMEI, just always query the classmark and dont worry about checking
				// whether we already have a valid classmark or not.
				if (gConfig.getBool("GSM.Cipher.Encrypt") || gConfig.getBool("Control.LUR.QueryClassmark"))  {
					timerStart(TMMCancel,12000,TimerAbortChan);
					channel()->l3sendm(L3ClassmarkEnquiry());
					return MachineStatusOK;
				} else {
					return callMachStart(new LUFinish(tran()));
				}
			}
		}

		case L3CASE_RR(ClassmarkChange): {
			timerStart(TMMCancel,12000,TimerAbortChan);
			const GSM::L3ClassmarkChange *resp = dynamic_cast<const GSM::L3ClassmarkChange*>(l3msg);
			const L3MobileStationClassmark2& classmark = resp->classmark();
			// We are storing the A5Bits for later use by CC, which is probably unnecessary because
			// it is included in the CC message.
			int A5Bits = classmark.getA5Bits();
			ludata()->store.setClassmark(A5Bits,classmark.powerClass());
			//gTMSITable.classmark(getImsiCh(),classmark);	// This one is going away; we'll update once later.

			if (gConfig.getBool("GSM.Cipher.Encrypt")) {
				// (pat) 9-2014 hack: GSML1FEC gets the Kc directly out of the tmsi table so we need to flush to the physical table
				// before sending the ciphering mode command.
				gTMSITable.tmsiTabUpdate(getImsi(),&ludata()->store);
				//int encryptionAlgorithm = gTMSITable.getPreferredA5Algorithm(getImsi().c_str());
				int encryptionAlgorithm = getPreferredA5Algorithm(A5Bits);
				if (!encryptionAlgorithm) {
					LOG(DEBUG) << "A5/3 and A5/1 not supported: NOT sending Ciphering Mode Command on " << *channel() << " for " << getImsiName();
				} else if (channel()->getL2Channel()->decryptUplink_maybe(getImsi(), encryptionAlgorithm)) {
					LOG(DEBUG) << "sending Ciphering Mode Command on " << *channel() << " for " << getImsiName();
					channel()->l3sendm(GSM::L3CipheringModeCommand(
						GSM::L3CipheringModeSetting(true, encryptionAlgorithm),
						GSM::L3CipheringModeResponse(false)));
					// We are now waiting for the cihering mode comlete command...
					return MachineStatusOK;	// The TMMCancel timer is running.
				} else {
					LOG(DEBUG) << "no ki: NOT sending Ciphering Mode Command on " << *channel() << " for " << getImsiName();
				}
			}
			return callMachStart(new LUFinish(tran()));
		}

		case L3CASE_RR(CipheringModeComplete): {
			// The fact the message arrived means success.  We hope.  Even if that were not true, we should proceed anyway.
			return callMachStart(new LUFinish(tran()));
		}

	default:
		return unexpectedState(state,l3msg);
	}
}


// ====== State Machine LUFinish  =====


MachineStatus LUFinish::machineRunState(int state, const GSM::L3Message* l3msg, const SIP::DialogMessage *sipmsg)
{
	LOG(DEBUG) <<"LUFinish" <<LOGVAR(state) <<tran();
	if (l3msg) { LOG(INFO) << *l3msg; }
	switch (state) {
		case stateStart:
			LOG(DEBUG) << channel()<< getImsi()<< getTmsi();
			return stateSendLUResponse();

		case stateLUAcceptTimeout: {
			LOG(DEBUG) << "LU Accept timeout for imsi:"<<getImsi();
			return statePostAccept();
		}
		case L3CASE_MM(TMSIReallocationComplete): {
			LOG(DEBUG) << "TMSI Reallocation Complate for imsi:"<<getImsi();
			if (! ludata()->mExpectingTmsiReallocationComplete) {
				LOG(ERR) << "unexpected TMSIReallocationComplete";
			} else {
				uint32_t newTmsi = ludata()->getTmsi();
				if (! newTmsi) {
					LOG(ERR) << "TMSI logic inconsistency";
				} else {
					LOG(DEBUG) <<LOGVAR(newTmsi);
					//gTMSITable.tmsiTabReallocationComplete(newTmsi);
					ludata()->store.setAssigned(1);
					// Putting the TMSI in the subscriber info is irrelevant.  This tran is going away momentarily,
					// but it should be in the MMContext, but even that doesnt matter because it wont be used again after initial authentication.
					tran()->subscriber().mTmsi = newTmsi;
				}
			}
			return statePostAccept();
		}

		default:
			return unexpectedState(state,l3msg);
	}
}

// N200 is number of LAPDm retransmissions, and is 34 on FACCH or 5 on SACCH.
// getMsg timeout is N200=34*T200ms=900 = 6.8s on FACCH,
// or N200-5*T200ms=900 = 4.5s on SACCH.
// That is not very useful because the MS times out if it does not receive a MM command in 10s in downlink,
// but maybe in uplink we can wait longer.

MachineStatus LUFinish::stateSendLUResponse()
{
	LOG(DEBUG);
	timerStart(TMMCancel,12000,TimerAbortChan);
	string imsi = this->getImsi();

	// We fail closed unless we're configured otherwise.
	// mRegistrationResult.regGetSuccess() is whether we are granting service.
	// rather than being allowed service due to network failure or open registration.
	Authorization authorization = AuthUnauthorized;
	MMRejectCause failCause = L3RejectCause::Zero;

	// (pat) TODO: We should store the authorization state of the welcome message that was sent so that when there is an
	// authorization state change (ie, from unauthorized to authorized) we can send a new welcome message.
	// But this should all be moved into sipauthserve anyway.
	if (ludata()->store.getWelcomeSent() == 0) {
		string stmp = gConfig.getStr("Control.LUR.RegistrationMessageFrequency");
		if (stmp == "PLMN") {
			// We only send the registration message if it is an imsi attach.
			// If it is a normal updating we assume a welcome message was sent by a different BTS, or possibly
			// earlier by us and the TMSI_TABLE database was lost.
			// If it is a periodic updating we assume a welcome message was sent by us but we lost the tmsi database somehow.
			if (! ludata()->isImsiAttach()) {
				ludata()->store.setWelcomeSent(2);	// welcome message sent by someone else
			}
		} else if (stmp == "NORMAL") {
			// We send the registration message the first time this BTS sees this MS.
			// If it is periodic updating, then we assume that we have seen the MS previously but our TMSI_TABLE database was lost.
			if (! ludata()->isInitialAttach()) {
				ludata()->store.setWelcomeSent(2);	// welcome message sent by us previously.
			}
		} else {
			// This is the stmp == 'FIRST' option.
			// We send the message if the WELCOME_SENT field is 0, regardless of the status reported by the MS.
		}
	}

	const char *openregistrationmsg = "";

	switch (ludata()->mRegistrationResult.mRegistrationStatus) {
		case RegistrationSuccess:
			authorization = AuthAuthorized;
			break;
		case RegistrationError:
			if (failOpen()) {
				authorization = AuthFailOpen;
				//ludata()->mRegistrationResult.regSetSuccess();
			} else {
				failCause = L3RejectCause::Network_Failure;
			}
			break;
		case RegistrationFail:
			// The OpenRegistration option does not distinguish between unrecognized and unauthorized imsis,
			// which is unfortunate.
			if (openRegistration()) {
				//ludata()->mRegistrationResult.regSetSuccess();
				authorization = AuthOpenRegistration;
				openregistrationmsg = "(open registration)";
			} else {
				failCause = ludata()->mRegistrationResult.mRejectCause;
			}
			break;
		default: devassert(0);
	}

	if (authorization != AuthUnauthorized) {
		if (authorization == AuthAuthorized) {
			LOG(INFO) << "registration SUCCESS"<<openregistrationmsg<<": " << ludata()->mLUMobileId;
		} else {
			LOG(INFO) << "registration ALLOWED: " << ludata()->mLUMobileId;
		}

		ludata()->store.setAuthorized(authorization);

		// This switch calls either tmsiTabUpdate or tmsiTabAssign to udpate the TMSI_TABLE.
		// We update the TMSI_ASSIGNED status in the TMSI_TABLE to reflect the phone's opinion, which could differ from the BTS.
		switch (ludata()->getTmsiStatus()) {
			case tmsiFailed:	
				// Getting here means we succeeded on the second attempt: TMSI failed but IMSI passed, ie, it was a TMSI collision.
				// Fall through...
			case tmsiNone: {
				// This is done only on the first registration in this BTS:
				// Allocate a new tmsi to go with the updated imsi.
				// Someday the tmsi may come from the registration server.
				//uint32_t newTmsi = gTMSITable.tmsiTabAssign(imsi,&ludata()->mLULAI,ludata()->mOldTmsi,&ludata()->store);
				uint32_t newTmsi = gTMSITable.tmsiTabCreateOrUpdate(imsi,&ludata()->store,&ludata()->mLULAI,ludata()->mOldTmsi);
				ludata()->setTmsi(newTmsi,tmsiNew);
				break;
			}

			case tmsiNotAssigned: {
				// The MS authenticated based on the IMSI even though it is already in the tmsi table.
				// If we are SendTMSIs is on, then either someone changed the option after this MS registered,
				// or maybe the MS just never received the TMSI assignment.
				// We may need to assign a new TMSI if the MS has just become registered and formerly had a fake tmsi,
				// or if the SendTMSIs option is on, so we call tmsiTabCreateOrUpdate instead of tmsiTabUpdate.
				//ludata()->setTmsiStatus(tmsiNew);  no need for this
				ludata()->store.setAssigned(0);		// Make sure TMSI database matches what the MS thinks.
				//gTMSITable.tmsiTabUpdate(imsi,&ludata()->store);
				uint32_t newTmsi1 = gTMSITable.tmsiTabCreateOrUpdate(imsi,&ludata()->store,&ludata()->mLULAI,ludata()->mOldTmsi);
				ludata()->setTmsi(newTmsi1,tmsiNotAssigned);	// Update to reflect possible new tmsi.
				break;
			}
			case tmsiProvisional:	// The TMSI from the tmsi table authenticated.
				ludata()->setTmsiStatus(tmsiAuthenticated);
				ludata()->store.setAssigned(1);
				gTMSITable.tmsiTabUpdate(imsi,&ludata()->store);
				break;

			case tmsiAuthenticated:
			case tmsiNew:
				// These cases should not occur here.
				devassert(0);
				LOG(ERR) <<"Unexpected TMSI state:"<< ludata()->getTmsiStatus();
				break;
		}

		// We update these values on every registration:
		// update: This is done by the registration engine now.
		//gTMSITable.putKc(tran()->subscriberIMSI().c_str(),ludata()->mKc, ludata()->mAssociatedUri, ludata()->mAssertedIdentity);

		LOG(DEBUG) <<LOGVAR(ludata()->getTmsi()) <<LOGVAR(ludata()->getTmsiStatus()) <<LOGVAR(tran()->subscriberIMSI());

		if (IS_LOG_LEVEL(DEBUG)) {
			TmsiStatus stat = ludata()->getTmsiStatus();
			assert(stat == tmsiNew || stat == tmsiNotAssigned || stat == tmsiAuthenticated);
			uint32_t ourTmsi = ludata()->getTmsi();
			string checkImsi = gTMSITable.tmsiTabGetIMSI(ourTmsi,NULL);
			string myimsi(tran()->subscriberIMSI());
			if (checkImsi != myimsi) {
				WATCH("TMSI Table insertion created TMSI collision for"<<LOGVAR(imsi)<<LOGVAR(checkImsi)
					<<LOGVAR(ludata()->getTmsi()) <<LOGVAR(configTmsiTestMode()));
			}
		}

	} else {
		assert(failCause != L3RejectCause::Zero);
		LOG(INFO) << "registration FAILED: " << ludata()->mLUMobileId << LOGVAR(failCause);
		devassert(imsi.size());
		//gTMSITable.tmsiTabSetRejected(imsi,failCause);
		ludata()->store.setRejectCode(failCause);
		gTMSITable.tmsiTabCreateOrUpdate(imsi,&ludata()->store,&ludata()->mLULAI,ludata()->mOldTmsi);
		channel()->l3sendm(L3LocationUpdatingReject(failCause));

		sendWelcomeMessage(ludata(), "Control.LUR.FailedRegistration.Message",	// Does nothing if the SQL var is not set.
				"Control.LUR.FailedRegistration.ShortCode",subscriber(),channel());

		// tmsiTabUpdate must be after sendWelcomeMessage optionally updates the welcomeSent field.
		gTMSITable.tmsiTabUpdate(imsi,&ludata()->store);

		//return closeChannel(L3RRCause::Normal_Event,RELEASE);
		return MachineStatus::QuitTran(TermCause::Local(failCause));
	}

	// (pat) We must NOT attach the MMContext to the MMUser during the Location Updating procedure;
	// Some MS (BLU phone) do not seem to be happy about starting an SMS on the same channel immediately after the LUR,
	// so we have to hang up the channel and re-page the MS to do the next procedure, which sucks.
	// gMMLayer.mmAttachByImsi(channel(),imsi);

	// Send the "short name" and time-of-day.
	string shortName = gConfig.getStr("GSM.Identity.ShortName");
	if (ludata()->isInitialAttach() && shortName.size()) {
		channel()->l3sendm(L3MMInformation(shortName.c_str()));
	}

	// Send LU Accept. Include a TMSI assignment, if needed.
	bool sendTMSIs = configSendTmsis();
	uint32_t newTmsi = ludata()->getTmsi();
	LOG(DEBUG) <<LOGVAR(ludata()->getTmsiStatus()) <<LOGVAR(ludata()->needsTmsiAssignment()) <<LOGVAR(sendTMSIs);
	if (ludata()->needsTmsiAssignment() && sendTMSIs && newTmsi) {
		ludata()->mExpectingTmsiReallocationComplete = true;
		// Send the TMSI assignment in the LU Accept.
		// (pat) This used to be 1 second but the BLU phone, for one, does not send the TMSI Reallocation complete fast enough.
		timerStart(TMisc1,5000,stateLUAcceptTimeout);
		L3MobileIdentity mid(newTmsi);
		// (pat 10-2013) I tried sending the welcome message on the same channel after sending the location updating
		// accept but the Blackberry just timed out and the BLU Deco Mini sent a SMS CP-ERROR.
		// Update: blackberry works sometimes.
		// I tried setting the follow-in proceed flag in the LocationUpdatingAccept and it did not help.
		channel()->l3sendm(L3LocationUpdatingAccept(gBTS.LAI(),mid,true));
		// (pat) This 1 second delay was in the original code, so I am duplicating it.
		// If we dont get the TMSIReallocationComplete within 1 second, go on to the next step anyway.
		// In the old code if it came later disaster could ensue, but now it would be ok.
		// Wait for MM TMSIReallocationComplete (0x055b).
		return MachineStatusOK;
	} else {
		// Do not send a TMSI assignment, just an LU Accept.
		channel()->l3sendm(L3LocationUpdatingAccept(gBTS.LAI(),true));
		return statePostAccept();
	}
}

MachineStatus LUFinish::statePostAccept()
{
	LOG(DEBUG);
	timerStop(TMisc1);	// The mystery timer.
	timerStop(TMMCancel);	// all finished.

	// If this is an IMSI attach, send a welcome message.
	// (pat) This should be in the sipauthserve, not the BTS.
	// (pat) We dont want to send the message on every IMSI attach, which happens whenever the phone is powered up.
	// We also dont really want to send the message if the message wanders into our cell from other Range cell.
	// So we only send the message if it is the first IMSI attach seen in this cell, which means we
	// need a special flag for this in the TMSI table.
	// For testing we can reset that flag.
	LOG(DEBUG) << LOGVAR(ludata()->isImsiAttach()) << LOGVAR(ludata()->getTmsiStatus()) << LOGVAR(ludata()->store.getWelcomeSent());
	if (ludata()->store.getAuth() == AuthAuthorized) {
		sendWelcomeMessage(ludata(), "Control.LUR.NormalRegistration.Message",
			"Control.LUR.NormalRegistration.ShortCode", subscriber(), channel());
	} else {
		sendWelcomeMessage(ludata(), "Control.LUR.OpenRegistration.Message",
			"Control.LUR.OpenRegistration.ShortCode", subscriber(), channel());
	}

	// tmsiTabUpdate must be after sendWelcomeMessage optionally updates the welcomeSent field.
	gTMSITable.tmsiTabUpdate(getImsi(),&ludata()->store);



	// Release the channel and return.
	LOG(DEBUG) <<"MM procedure complete";
	return MachineStatus::QuitTran(TermCause::Local(L3Cause::MM_Success));
}

// The l3msg is LocationUpdatingRequest
void LURInit(const GSM::L3Message *l3msg, MMContext *mmchan)
{
	LOG(DEBUG) << mmchan;
	TranEntry *tran = TranEntry::newMOMM(mmchan);

	LOG(DEBUG) <<"lockAndStart" <<LOGVAR2("tid",tran->tranID());
	tran->lockAndStart(new LUStart(tran),(GSM::L3Message*)l3msg);
}

// ====== State Machine LUNetworkFailure =====

MachineStatus LUNetworkFailure::machineRunState(int state, const GSM::L3Message* l3msg, const SIP::DialogMessage *sipmsg)
{
	switch (state) {
		case stateStart:
			PROCLOG(ALERT)<< "SIP authentication timed out.  Is the proxy running at " << gConfig.getStr("SIP.Proxy.Registration");
			// Reject with a "network failure" cause code, 0x11.
			gReports.incr("OpenBTS.GSM.MM.LUR.Timeout");
			// (pat) FIXME: I am faithfully duplicating the 4 second delay, but we should find out what
			// message we are expecting so we can finish if we see it.
			// Is this T3213 - location updating failure in the MS?
			// (pat) I believe this 4 delay is supposed to be T3111, but is it inapplicable to Location Updating;
			// even though it is defined in the RR timers, I think it is only applicable to a CC "L3Disconnect" because
			// the reason cited for the delay is to allow time for additional messages, but there would not be any for a low level delay.
			// There is a T3111 timer on channel release code in GSML1FEC, and this is redundant.
			// Another thought: maybe the channel close prejudicially closes the LAP2Dm communication, and this is to give LAP2Dm a chance to
			// get the message through.
			//onTimeout1(4000,stateAuthFail);
			timerStart(TMMCancel,4000,TimerAbortChan);	// Mystery timer.
			// We dont unauthorize because it is not the MS fault.
			channel()->l3sendm(L3LocationUpdatingReject(L3RejectCause::Network_Failure));
			return MachineStatusOK;
		default:
			return unexpectedState(state,l3msg);
	}
}

// ====== State Machine L3RegisterMachine =====

L3RegisterMachine::L3RegisterMachine(TranEntry *wTran,
	SIP::DialogType wMethod,
	string &wSRES,				// may be NULL for the initial registration query to elicit a 
	RegistrationResult *wRResult					// Result returned here: true (1), false(0), timeout (-1).
) :
	LUBase(wTran),
	mSRES(wSRES),
	mRResult(wRResult)
{
	PROCLOG(DEBUG)<<"ProcedureRegister"<<LOGVAR(mRResult->text())<<LOGVAR(mSRES);
	// This procedure is invoked twice, now using the same SIPEngine in the TranEntry.
	// Must make a new call_id each time we invoke this procedure.
	setDialog(getRegistrar());
}


// This duplicates the existing functionality.
// RFC-3261 sec 10.2: "A REGISTER request does not establish a dialog."  A dialog normally has both inbound and outbound callids
// for two way communication, but the registrar sends only a single response to the REGISTER method so there is no dialog,
// so we do not need an outbound callid to respond to the registrar.

MachineStatus L3RegisterMachine::machineRunState(int state, const GSM::L3Message* l3msg, const SIP::DialogMessage *sipmsg)
{
	switch (state) {
		case stateStart:		// Start state.
			startRegister(tran()->tranID(),tran()->subscriber(),mRResult->mRand,mSRES,channel());
			return MachineStatusOK;

		case L3CASE_SIP(dialogActive): {
			int status = sipmsg->sipStatusCode();
			const DialogAuthMessage *amsg = dynamic_cast<typeof(amsg)>(sipmsg);
			if (amsg == NULL) {
				LOG(ERR) << "L3RegisterMachine could not convert DialogAuthMessage " << sipmsg;
				mRResult->regSetError();
				return MachineStatusPopMachine;
			}
			// This should be an assert, but we dont want to crash:
			if (status != 200) { PROCLOG(ERR) << "unexpected"<<LOGVAR(status)<<" in dialog message"; }
			//gTMSITable.putKc(tran()->subscriberIMSI().c_str(),amsg->dmKc, amsg->dmPAssociatedUri, amsg->dmPAssertedIdentity);
			ludata()->store.setKc(amsg->dmKc);
			ludata()->store.setAssociatedUri(amsg->dmPAssociatedUri);
			ludata()->store.setAssertedIdentity(amsg->dmPAssertedIdentity);
			PROCLOG(INFO) << "REGISTER success";
			mRResult->regSetSuccess();
			return MachineStatusPopMachine;
		}
		case L3CASE_SIP(dialogFail): {
			int sipCode = sipmsg->sipStatusCode();
			switch (sipCode) {
				case 401: {	// SIP 401 "Unauthorized"
					//string wRANDresponse = SIP::randy401(sipmsg);
					const DialogChallengeMessage *challenge = dynamic_cast<typeof(challenge)>(sipmsg);
					if (challenge == NULL) {
						LOG(ERR) << "L3RegisterMachine could not convert DialogChallengeMessage " << sipmsg;
						mRResult->regSetError();
						return MachineStatusPopMachine;
					}
					string wRANDresponse = challenge->dmRand;
					// if rand is included on 401 unauthorized, then the challenge-response game is afoot
					if (wRANDresponse.length() != 0) {
						PROCLOG(INFO) << "REGISTER challenge RAND=" << wRANDresponse;
						mRResult->regSetChallenge(wRANDresponse);
						break;
					} else {
						// The Registrar disallowed this IMSI without a challenge.
						goto defaultcase;
					}
					devassert(0);	// We do not arrive here.
					break;
				}
				default:
				defaultcase:
					// If the Registrar specified the reject code in our SIP private header, use it, otherwise
					// translate the SIP result code into a reject cause using getRejectCause().
					const DialogChallengeMessage *challenge = dynamic_cast<typeof(challenge)>(sipmsg);
					MMRejectCause rejectCause = L3RejectCause::Zero;	// unused init to shut up gcc.
					if (challenge && challenge->dmRejectCause) {
						rejectCause = (MMRejectCause)(int)challenge->dmRejectCause;

#if 0	// (pat) Please dont enable this.  See comments at queryForRejectCause
#endif
					} else {
						rejectCause = getRejectCause(sipCode);
					}
					mRResult->regSetFail(sipCode,rejectCause);
					PROCLOG(INFO) << "REGISTER fail -- unauthorized" <<LOGVAR(sipCode) <<LOGVAR(rejectCause);
					break;
			}
			return MachineStatusPopMachine;
		}
		default:
			return unexpectedState(state,l3msg);
	}
}

string RegistrationResult::text()
{
	string result = format("RegistrationStatus=%u",mRegistrationStatus);
	if (mRegistrationStatus == RegistrationFail) { result += format(",SipCode=%u,RejectCause=%u",mSipCode,mRejectCause); }
	return result;
}

// (pat) 5-2014.  We dont currently save the detach information in OpenBTS.  I dont want to set the TMSI table AUTH or AUTH_EXPIRY to 0,
// because we may need those for a later authorization if backhaul is cut or central authorization entity needs to be
// refreshed from our database.  I dont want to add a new TMSI table field at this time, since it outdates all existing TMSI tables.
// The best way would would be to make AUTH a bit field and add bits.
// However, we dont currently use that information;  we could send an immediate final response code to an INVITE or MESSAGE
// for a handset that did an Imsi-Detach if the beacon AttachDetach flag is still set, however, that will not work when we support
// multiple BTS per LAC - the final response should be sent from the central authority, not the individual BTS.
// So I am going to do nothing else at this time.
// (pat) NOTE:  Kazoo may send a dialog failure to the imsi detach, which will try to go to the transaction, and is currently ignored.
void imsiDetach(L3MobileIdentity mobid, L3LogicalChannel *chan)
{
	string imsi;
	if (mobid.isIMSI()) {
		imsi = string(mobid.digits());
	} else if (mobid.isTMSI()) {
		imsi = gTMSITable.tmsiTabGetIMSI(mobid.TMSI(),NULL);
		if (imsi.size() == 0) {
			LOG(WARNING)<<format("IMSI Detach indication with unrecognized TMSI (0x%x) ignored",mobid.TMSI());
			return;
		}
	} else {
		LOG(WARNING)<<format("IMSI Detach indication with unrecognized mobileID type (%d) ignored",mobid.type());
		return;
	}
	startUnregister(imsi,chan);
}

}; // namespace Control
