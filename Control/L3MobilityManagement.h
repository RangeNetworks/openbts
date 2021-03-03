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

#ifndef _L3MOBILITYMANAGEMENT_H_
#define _L3MOBILITYMANAGEMENT_H_ 1

#include <GSMCommon.h>
//#include <memory>	// for auto_ptr, shared_ptr
#include "L3StateMachine.h"
#include "TMSITable.h"
#include <SIPBase.h>
#include <GSML3MMElements.h>


namespace SIP { class DialogMessage; };

namespace Control {
using namespace GSM;

void NewPagingResponseHandler(const L3PagingResponse* resp, MMContext* mmchan);
void NewCMServiceResponder(const L3CMServiceRequest* cmsrq, MMContext* mmchan);



// (pat) LISTEN UP!  Before you touch this class hiearchy:
// There is a major fauxpax in C++ that all virtual classes bypass the class hierarchy and are finalized only in the most derived class.
// I dont see any implementation reason for this, so it is just speced this way arbitrarily.
// Therefore if a virtual method in a virtual base class is over-ridden in an intermediate class in a hierarchy involving multiple paths
// to a final derived class, there must also be a unique method defined in the final derived class.
// This is true if any class in the path is virtual, so the the virtual class basically contaminates the hierarchy
// and makes virtual methods nearly worthless in that hierarchy.  It is a pretty big oops.
// The consequence for us in our attempt to create state machines using protected methods to implement the states is that
// it creates a problem with shared data.  The shared data wants to be in a virtual class common to all the classes implementing state machines.
// However, that same virtual class cannot be in the path of the virtual methods implementing the states.
// Therefore the shared data must be in a separate class so that the main path to virtual methods is not contaminated by 'virtual' anywhere.
// The alternatives are either to put the data in a separately allocated class and use pointers, or put virtual accessor methods in 
// the LUBase class and the data itself in the most derived class.
// Another problem to beware is if the final derived class defines a pure virtual method, that seems to over-ride the definition of the same method
// in the intermediate classes, which may be a bug.

class L3IdentifyMachine : public MachineBase
{
	const GSM::L3MobileIdentity mMobileID;	// We make a copy because the original disappears.
	bool *mResultPtr;

	protected:
	enum States {
		stateStart,
	};
	MachineStatus machineRunState(int state, const GSM::L3Message *l3msg, const SIP::DialogMessage*sipmsg);

	public:
	// On success the resultant imsi is placed in wTran->subscriber.
	L3IdentifyMachine(TranEntry *wTran,
		const GSM::L3MobileIdentity &wMobileID,
		bool *wResultPtr)		// Returns true on success, false on failure.
		: MachineBase(wTran), mMobileID(wMobileID), mResultPtr(wResultPtr)
		{}

	const char *debugName() const { return "L3IdentifyMachine"; }
};

enum RegistrationStatus {
	RegistrationUninitialized,
	// We distinguish the "Error" case from "Fail" case, the latter meaning rejection by Registrar.
	RegistrationError,			// Something went wrong: network failure, invalid SIP message, etc.
	RegistrationChallenge,
	RegistrationSuccess,
	RegistrationFail,	// Rejected by Registrar, in which case the mSipCode tells why.
};

struct RegistrationResult {
	RegistrationStatus mRegistrationStatus;	// Over-all result from Registrar.
	MMRejectCause mRejectCause;	// Only if mRegistrationStatus == RegistrationFail.
	unsigned mSipCode;		// Registration result SIP code, used only for error messages.
	string mRand;			// The authentication challenge rand.
	RegistrationResult() : mRegistrationStatus(RegistrationUninitialized), mRejectCause(L3RejectCause::Zero) , mSipCode(0)
		{}
	void regSetSuccess() { mRegistrationStatus = RegistrationSuccess; }
	void regSetError() { mRegistrationStatus = RegistrationError; }
	void regSetFail(unsigned sipCode, MMRejectCause cause) { mRegistrationStatus = RegistrationFail; mSipCode = sipCode; mRejectCause = cause; }
	void regSetChallenge(string wRand) { mRegistrationStatus = RegistrationChallenge; mRand = wRand; }
	bool isValid() { return mRegistrationStatus != RegistrationUninitialized; }
	bool isSuccess() { return mRegistrationStatus == RegistrationSuccess; }
	string text();
};

// Follow on Proceed flag in LocationUpdateAccept, dont release RR connection until T3255, value not specified in spec.

// Making the LUStart be base classes did not work because
// we need a final override for each of the states in each virtual base class.
// The MMSharedData must not contain MachineBase as a base class because virtual methods
// and virtual classes do not mix well.

// MS sends IMSI
//	A. authorize
//  if authorization failure:
//		TMSITable set AUTH_STATUS = 0.  Do we want to delete the record?
//		exit
//	if authorization success:
//		if existing record:
//			TMSITable update lai, classmark, AUTH_STATUS
//			send assignedTmsi to handset.
//		else:
//			assignedTmsi = allocate new TMSI.
//			TMSITable update everything.
//			send newTmsi to handset.
// MS sends TMSI
//	oldTmsi = tmsi.
//	TMSITable get IMSI for oldTmsi.
//	if no record, get IMSI.
//	authorize
//		if authorization success
//			TMSITable update accessed, and everything else in case it changed.
//		auth failure:
//			We do not delete the record because it may belong to someone else.
//			get IMSI
//			if IMSI matches record, exit.
//			goto A.
//          

	// The LUR may have been using a TMSI or IMSI.
	// If we get a TMSI we try authentication, but if we fail, it may be a TMSI collision,
	// so at that point we have to query for IMSI and retry authorization.
	enum TmsiStatus {
		tmsiNone,			// We dont have a TMSI.
		tmsiProvisional,	// MS sent a TMSI that was found in the TMSI table but has not been authenticated yet.
		tmsiAuthenticated,	// MS sent a TMSI that was found in the TMSI table (started as tmsiProvisional) and authenticated ok.
		tmsiNotAssigned,	// MS sent an IMSI that was found in the TMSI table.
		tmsiFailed,			// TMSI failed authentication.
		tmsiNew,			// We allocated a new TMSI for this MS.
	};

	// This class holds the common data for all LocationUpdating sub-states.
	// We save up everything until we have authenticated the MS and then put it in the TMSI table only if MS authenticates ok.
	// Previously it was included as virtual by every Procedure, but now it lives in the TransactionEntry.
	class MMSharedData {
		uint32_t mAssignedTmsi;	// a TMSI that has been assigned to the MS either now or in the past.
		TmsiStatus mTmsiStatus;
		public:
		GSM::L3MobileIdentity mLUMobileId;	// Copy saved from original request.
		GSM::L3LocationAreaIdentity mLULAI;	// Copy saved from original request.
		LocationUpdateType mLUType;
		uint32_t mOldTmsi;		// The tmsi sent in LUR, which is irrelevant if the LAI is not ours, but saved only for reporting purposes.
		//string mRAND;
		TmsiTableStore store;	// In-memory storage for stuff in the TMSI_TABLE.
		GSM::MobileIDType mQueryType;	// What mobileId did we last request: IMSIType or IMEIType?
		RegistrationResult mRegistrationResult;
		// If we received or queried for IMSI (as opposed to registration by TMSI) we set mFullQuery so that
		// in this case we will also optionally query for IMEI.
		Bool_z mFullQuery;
		// The second attempt occurs if registration by TMSI fails, so we have to register by IMSI.
		// This has nothing to do with keeping track of the 2 register messages for each over-all registration attempt.
		Bool_z mSecondAttempt;
		string mPrevRegisterAttemptImsi;
		Bool_z mExpectingTmsiReallocationComplete;
#if CACHE_AUTH
		Bool_z mUsingCachedAuthentication;
#endif

		// Only prints the subset that is interesting for debugging.
		string text() {
			return format("AssignedTmsi=0x%x status=%d RegistrationResult=%s",mAssignedTmsi,mTmsiStatus,
				mRegistrationResult.text().c_str());
			//return format("AssignedTmsi=0x%x status=%d RegistrationResult=%s rand=%s",mAssignedTmsi,mTmsiStatus,
			//	mRegistrationResult.text().c_str(),mRAND.c_str());
		}

		// You must set the tmsistatus of any tmsi you assign, so we wrap that in a method.
		void setTmsi(uint32_t tmsi,TmsiStatus status) { mAssignedTmsi = tmsi; mTmsiStatus = status; }
		// But you can update the tmsi status without changing the tmsi.
		void setTmsiStatus(TmsiStatus status) { mTmsiStatus = status; }
		TmsiStatus getTmsiStatus() { return mTmsiStatus; }

		//bool haveTmsi() { return mTmsiStatus != tmsiNone && mTmsiStatus ; }
		// Is this a brand new registration?
		// FIXME THIS needs to check that authorization has changed
		//		Need two variables: one for new authorization, one for welcome message.
		// bool isFirstTime() { return mTmsiStatus == tmsiNone || mTmsiStatus == tmsiNew; }
		bool isImsiAttach() { return this->mLUType == LUTImsiAttach; }
		// Is this the initial attach on this BTS?  Any attach type except periodic updating.
		bool isInitialAttach() { return this->mLUType == LUTImsiAttach || this->mLUType == LUTNormalLocationUpdating; }
		bool needsTmsiAssignment() { return mTmsiStatus == tmsiNew || mTmsiStatus == tmsiNotAssigned; }
		uint32_t getTmsi() { return mAssignedTmsi; }

		MMSharedData() :
			mAssignedTmsi(0), mTmsiStatus(tmsiNone), mOldTmsi(0), mQueryType(GSM::NoIDType)
			{}
	};

	class LUBase: public MachineBase {
		protected:
		MMSharedData* ludata() const;
		public:
		LUBase(TranEntry *tran) : MachineBase(tran) {}
		//L3RejectCause getRejectCause();
		bool openRegistration() const;
		bool failOpen() const;
		// Return a persistent IMSI string that will not go away
		string getImsi() const;
		const char * getImsiCh() const;
		uint32_t getTmsi() const { return ludata()->getTmsi(); }
		const string getImsiName() const;
		FullMobileId &subscriber() const;
	};

	// Initial identification phase of LU - Location Updating.
	class LUStart : public LUBase /*, public virtual LUSharedData*/ {
		MachineStatus sendQuery(GSM::MobileIDType);
		public:
		enum State {		// There is no integral start state because the state machine start state receives the LUR message.
			stateSecondAttempt,
			stateHaveImsi,
			stateHaveIds,
			stateRegister1Response,
		};
		MachineStatus stateRecvLocationUpdatingRequest(const GSM::L3LocationUpdatingRequest*);
		MachineStatus stateRecvIdentityResponse(const GSM::L3IdentityResponse *);
		//MachineStatus stateExpiredT3260();
		public:
		MachineStatus machineRunState(int state, const GSM::L3Message* l3msg=0, const SIP::DialogMessage *sipmsg=0);
		public:
		LUStart(TranEntry *wTran) : LUBase(wTran) {}
		friend class L3ProcedureLocationUpdate;
		const char *debugName() const { return "LUStart"; }
	} /*mLUStart*/;

	class LUAuthentication: public LUBase /*, public virtual LUSharedData*/ {
		enum States {
			stateStart,
			stateRegister2Response
		};

		MachineStatus machineRunState(int state, const GSM::L3Message* l3msg=0, const SIP::DialogMessage *sipmsg=0);
		public:
		LUAuthentication(TranEntry *wTran) : LUBase(wTran) {}
		const char *debugName() const { return "LUAuthentication"; }
	} /*mLUAuthentication*/;

	class LUFinish: public LUBase /*, public virtual LUSharedData*/ {
		enum States {
			stateStart,
			stateRegister2Response,
			stateLUAcceptTimeout,
		};
		MachineStatus stateExpiredT3270();
		MachineStatus machineRunState(int state, const GSM::L3Message* l3msg, const SIP::DialogMessage *sipmsg=0);
		MachineStatus statePostAccept();
		MachineStatus stateQueryClassmark();
		MachineStatus stateSendLUResponse();
		//MachineStatus stateLUAcceptTimeout();
		public:
		LUFinish(TranEntry *wTran) : LUBase(wTran) {}
		const char *debugName() const { return "LUFinish"; }
	} /*mLUFinish*/;

	class LUNetworkFailure: public LUBase /*, public virtual LUSharedData*/ {
		enum States {
			stateStart
		};
		MachineStatus machineRunState(int state, const GSM::L3Message* l3msg, const SIP::DialogMessage *sipmsg=0);
		public:
		const char *debugName() const { return "LUNetworkFailure"; }
		LUNetworkFailure(TranEntry *wTran) : LUBase(wTran) {}
	} /*mLUNetworkFailure*/;

void LURInit(const GSM::L3Message *l3msg, MMContext *dcch);


class L3RegisterMachine : public LUBase //MachineBase
{
	// I started using the engine in the transaction, but we have to create
	// a new callid for each transaction, and the easiest way was to create a new SIPEngine.
	// Update: Now just changing the call_id of the SIPEngine
	string mSRES;
	RegistrationResult *mRResult;

	protected:
	enum States {		// Only state 0 is used, so dont bother with an enum.
		stateStart,
	};
	MachineStatus machineRunState(int state, const GSM::L3Message *l3msg, const SIP::DialogMessage*sipmsg);

	public:
	L3RegisterMachine(TranEntry *wTran,
		SIP::DialogType wMethod,
		string &wSRES,				// may be NULL for the initial registration query to elicit a 
		RegistrationResult *wResult					// Result returned here: true (1), false(0), timeout (-1).
	);
	const char *debugName() const { return "L3RegisterMachine"; }
	SIP::SipMessage *makeRegisterMsg1();
};

extern void imsiDetach(L3MobileIdentity mobid, L3LogicalChannel *chan);

}; // namespace Control
#endif
