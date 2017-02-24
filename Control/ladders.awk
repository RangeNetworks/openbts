#!/bin/sh

# Draw ladder diagrams.  Used for the MOC, MTC, etc diagrams

# First, copy the program part of this file itself:
cp $0 $0.bk
sed -n "1,/^'.*EOF/p" $0


awk '
BEGIN { FS = "|";
	spaces = "                                                          ";
	dashes = "----------------------------------------------------------";
	split("0|40|30|30",WIDTH)
}
# Center arg in a width field, and add arrows if necessary.
function debug (a,b,c,e,f) { if (0) print a,b,c,d,e,f }
function fixup(arg,width,  filllen1) {
	leftarrow = (arg ~ /</);
	rightarrow = (arg ~ />/);
	double = (arg ~ /<<|>>/);
	filler = spaces;
	if (leftarrow || rightarrow) {
		filler = dashes;
		gsub(/(^[<-][<-]*)|([->]*[->]$)/,"",arg)
		#gsub(/(^<-*)|(^[->]*)|([->]*$)/,"",arg)
	}
	gsub(/^ *| *$/,"",arg)
	width -= leftarrow + rightarrow + double
	filllen1 = (width - length(arg))/2
	debug("fixup(",arg,width,filllen1,")")
	arg = substr(filler,1,filllen1) arg
	arg = arg substr(filler,1,width - length(arg))
	if (leftarrow) { if (double) {sub(/^/,"<<",arg)} else {sub(/^/,"<",arg)} }
	if (rightarrow) { if (double) {sub(/$/,">>",arg)} else {sub(/$/,">",arg)} }
	#return sprintf("%" width "." width "%s",arg);
	return arg
}
/^#/ || /^[^|]*$/ { print;next }
/^WIDTH/ {
	print	# Preserve this command in the output.
	sub("WIDTH *","")
	debug("BEFORE, WIDTH[1]=" WIDTH[1])
	split($0,WIDTH)
	debug("AFTER, WIDTH[1]=" WIDTH[1])
	debug("$0=" $0)
	next
}
{
	spaces = "                                                 "
	# Note: $1 is the stuff before the first |, which we just ignore.
	w[1] = WIDTH[1]; w[2] = WIDTH[2]; w[3] = WIDTH[3]; w[4] = WIDTH[4]
	arg0 = fixup($1,WIDTH[1])
	if ($2 ~ />>|<</) {
		w[2] = WIDTH[2] + WIDTH[3] + 1
		w[3] = WIDTH[4]
	} else if ($3 ~ />>|<</) {
		w[3] = WIDTH[3] + WIDTH[4] + 1
		# Does the second field span two sections?
	}
	debug("WIDTHS:",w[1],w[2],w[3],w[4])
	if (NF <= 2) {
		print
	} else if (NF == 3) {
		printf("%" w[1] "s|%s|%s\n",$1,fixup($2,w[2]),$3)
	} else if (NF == 4) {
		printf("%" w[1] "s|%s|%s|%s\n",$1,fixup($2,w[2]),fixup($3,w[3]),$4)
	} else if (NF >= 5) {
		printf("%" w[1] "s|%s|%s|%s|%s\n",$1,fixup($2,w[2]),fixup($3,w[3]),fixup($4,w[4]),$5)
		#arg1 = (length($2)) ? fixup($2,40) : ""
		#arg2 = (length($3)) ? fixup($3,30) : ""
		#sub(/^[ \t]*/," ",$4)
		#printf("%s|%40.40s|%30.30s|%s\n",$1, arg1,arg2,$4)
	}
}
END { print "EOF" }
' << \EOF
===================================================================================
See GSM 4.08 (or new 23.108) sec 7.3
Handset                              OpenBTS                      SIP Switch
WIDTH 0|40|30
========= MOC Mobile-Originated Call
AccessGrantResponsder decodes the chtype request, and if a voice call:
	gets a channel (TCH or SDCCH), opens the LogicalChannel, LAPDm waits for a SABM establish, then sends ESTABLISH primitive to L3.
|---------ChannelRequest(RACH)---------->|                              | 
|<-------ImmediateAssignment(AGCH)-------|                              | AccessGrantResponder
|   allocate Control.VEA ? TCH : SDCCH   |                              | decodeChannelNeeded
|CMServiceRequest(veryEarly?FACCH:SDCCH)>|                              | DCCHDispatcher,DCCHDIspatchMessage,DCCHDispatchMM,CMServiceResponder calls MOCStarter
|<------------IdentityRequest------------|                              | only if TMSI unrecognized
|-----------IdentityResponse------------>|                              | 
|         Requests MM Connection         |                              | MS CCState=MMPending, but that doesnt matter to us.
== MOCStarter
|<-------Authentication Procedure------->|                              | resolveIMSI
|           Allocate TCH,FACH            |                              | MOCStarter
|<------------CMServiceAccept------------|                              | MOCStarter
|       Establishes MM Connection        |                              | MS CCState=MMPending, but that doesnt matter to us.
|-------------L3Setup(SDCH)------------->|                              | MOCStarter
|          new TranscationEntry          |       allocateRTPPorts       | MOCStarter
|         GSMState=MOCInitiated          |      SIPState=NullState      | constructors
|<----------CC-CALL PROCEEDING-----------|-----------INVITE------------>| MOCStarter,MOCSendINVITE
|<-----------ChannelModeModify-----------|      SIPState=Starting       | MOCStarter if veryEarly
|---------ChannelModeModifyAck---------->|                              | MOCStarter if veryEarly
|            call assignTCHF             |                              | (used only for EA; for VEA we assigned TCH in AccessGrantResponder)
|<----------L3AssignmentCommand----------|                              | assignTCHF, repeated until answered
assignTCHF handles FACCH and waits for call to terminate while DCCHDispatchter handles DCCH.  Both call callManagementDispatchGSM
	timeout on DCCH.
|-------AssignmentComplete(FACCH)------->|                              | DCCHDispatchRR,AssignmentCompleteHandler, calls MOCController or MTCController
|       call MOC or MTCController        |                              | AssignmentCompleteHandler
== MOCController
while GSMState != CallReceived { switch (SIPState)
	case SIPState==MOCBusy:
|                                        |<----------486 Busy-----------| MOCCheckForOk
|                                        |       SIPState = Busy        | MOCCheckForOK
|                                        |-------------ACK------------->| MOCController,MOCSendAck
|<-------------L3Disconnect--------------|                              | forceGSMClearing
|<-----------L3ReleaseComplete-----------|                              | forceGSMClearing
|<-----------L3ChannelRelease------------|                              | forceGSMClearing
|           GSMState=NullState           |                              | forceGSMClearing
	case SIPState==Fail:
|                                        |<------------many-------------| MOCCheckForOK
|                                        |        SIPState=Fail         | MOCCheckForOK
|            forceGSMClearing            |       forceSIPClearing       | abortAndRemoveCall,abortCall
	case SIPState==Ringing
|                                        |<---------180 Ringing---------| MOCCheckForOK
|                                        |       SIPState=Ringing       | MOCCheckForOK
|        GSMState = CallReceived         |        MOCController         | 
|<---L3Alerting GSMState=CallReceived----|                              | MOCController
	Note: Call Waiting notification in Alerting message.
	case SIPState==Active
|                                        |<-----------200 OK------------| MOCCheckForOK
|                                        |       SIPState=Active        | MOCCheckForOK
|         GSMState=CallReceived          |                              | MOCController
	case SIPState==Proceeding
|                                        |<---------100 Trying----------| MOCCheckForOK
|                                        |<--------OR 181 Trying--------| MOCCheckForOK
|                                        |<--------OR 182 Queued--------| MOCCheckForOK
|                                        |<---OR 183 Session Progress---| MOCCheckForOK
|                                        |     SIPState=Proceeding      | MOCCheckForOK
|<--------------L3Progress---------------|                              | MOCController
	case SIPState==Timeout
|                                        |--------resend INVITE-------->| MOCResendINVITE
|                                        |       continue waiting       | 
} // end while

// Wait for SIP session to start
while SIPState != Active { switch (SIPState)
	case SIPState==Busy:
|                                        |<----------600 busy-----------| MOCCheckForOK
|                                        |-------------ACK------------->| MOCCheckForOK,MOCSendACK
|           abortAndRemoveCall           |                              | 
	case SIPState==Fail:
|                                        |<------------many-------------| MOCCheckForOK
|                                        |-------------ACK------------->| MOCCheckForOK,MOCSendACK
|           abortAndRemoveCall           |                              | 
	case SIPState==Proceeding:
|                                        |<---------100 Trying----------| MOCCheckForOK
|                                        |<--181 CallIsBeingForwarded---| MOCCheckForOK
|                                        |<--182 CallIsBeingForwarded---| MOCCheckForOK
|                                        |<-----183 SessionProgress-----| MOCCheckForOK
|                continue                |                              | MOCController
	case SIPState==Timeout:
|       break, let MS deal with it       |                              | MOCController
	case SIPState==Timeout:
|             break, success             |                              | MOCController
} // end while

|<---------------L3Connect---------------|                              | MOCController
|       GSMState=ConnectIndication       |                              | MOCController
|                                        |           init rtp           | MOCInitRTP
|                                        |-------------ACK------------->| MOCSendACK
while GSMState != Active { switch (GSMState))
|-----L3ConnectAcknowledge(either)------>|                              | MOCController,updateGSMSignalling,callManagementDispatchGSM
|            GSMState=Active             |                              | MOCController,callManagementDispatchGSM
} // end while
|        call callManagementLoop         |                              | MOCController


================ MTC Procedure
|                                        |<-----------INVITE------------| checkINVITE called from drive
|          new TransactionEntry          |                              | checkINVITE
|     if (callWaiting) goto L3Setup      |                              | 
|              pager.addID               |                              | checkINVITE
|            GSMState=Paging             |                              | Pager::addID
|<-------------PagingRequst--------------|                              | pageAll
|---------ChannelRequest(RACH)---------->|                              | 
|<-------ImmediateAssignment(AGCH)-------|                              | AccessGrantResponder
|----PagingResponse(SDCCH or FACCH)----->|                              | DCCHDispatchRR, PagingResponseHandler
|            call MTCStarter             |                              | PagingResponseHandler
== MTCStarter
|       if (!veryEarly) alloc TCH        |                              | MTCStarter if (!veryEarly)
|<-------optional authentication-------->|                              | not in OpenBTS
|<----------------L3Setup----------------|                              | MTCStarter
|          GSMState=CallPresent          |                              | MTCStarter
|                                        |---------100 Trying---------->| MTCSendTrying
|                                        |    SIPState = Proceeding     | 
|-------------CallConfirmed------------->|                              | updateGSMSignalling,callManagementDispatchGSM
|        GSMState=MTCallConfirmed        |                              | MTCStarter
	if (veryEarly) {
|<-----------ChannelModeModify-----------|                              | MTCStarter
|---------ChannelModeModifyAck---------->|                              | MTCStarter
|           call MTCController           |                              | MTCStarter
	} else {
|            call assignTCHF             |                              | MTCStarter
|<----------L3AssignmentCommand----------|                              | assignTCHF, repeated until answered
|-------AssignmentComplete(DCCH)-------->|                              | DCCHDispatchRR,AssignmentCompleteHandler, calls MOCController or MTCController
|       call MOC or MTCController        |                              | AssignmentCompleteHandler
	}
== MTCController (early assignment)
|--------------L3Alerting--------------->|                              | callManagementDispatchGSM from updateGSMSignalling from MTCController
|         GSMState=CallReceived          |                              | 
|                                        |-----------Ringing----------->| MTCSendRinging
|---------------L3Connect--------------->|                              | callManagementDispatchGSM from updateGSMSignalling from MTCController
|          old:GSMState=Active           |                              | 
|                                        |      allocate rtpports       | MTCController
|                                        |-------200 OK with SDP------->| MTCController,MTCSendOK,sip_okay_sdp
|                                        |     SIPState=Connecting      | 
|                                        |<-------------ACK-------------| MTCCheckForACK
|                                        |       SIPState=Active        | ?
|<-------------L3ConnectAck--------------|                              | MTCController
|            GSMState=Active             |                              | 
|        call callManagementLoop         |                              | MTCController


======= updateGSMSignalling, called many places in MOCController.  What messages occur here?
======= callManagementDispatchGSM, called from updateGSMSignalling for DCCH and assignTCH for FACCH messages.
|--------L3CallConfirmed(either)-------->|                              | callManagementDispatchGSM, is this used?
|          GSMState=MTConfirmed          |                              | callManagementDispatchGSM is this used?
|----------L3Alerting(either)----------->|                              | callManagementDispatchGSM is this used?
|         GSMState=CallReceived          |                              | callManagementDispatchGSM
|--------------many others-------------->|                              | callManagementDispatchGSM
|-----------L3Connect(either)----------->|                              | MOCController,callManagementDispatchGSM,GSMState=Active  I think this is impossible, a bug in the code.

================ Disconnect Procedure
|----------L3Disconnect(DCCH)----------->|                              | 
TODO

================ Authentication Procedure
|<--------Identity Request(SDCCH)--------|                              | resolveIMSI
|-------Identity Response(SDCCH)-------->|                              | resolveIMSI
|<----------Auth Req. (SDCCH)?-----------|                              | We skip, see MOCStarter
|----------Auth Resp (SDCCH)?----------->|                              | We skip, see MOCStarter
|<------RR-Cipher Mode Cmd (SDCCH)-------|                              | We skip, see MOCStarter
|----RR-Cipher Mode Complete (SDCCH)---->|                              | We skip, see MOCStarter
|<---MM-TMSI Reallocation Cmd (SDCHH)----|                              | We dont use
|-MM-TMSI Reallocation Complete (SDCHH)->|                              | We dont use


============ MT-SMS
|                                        |<-----------MESSAGE-----------| checkINVITE

========= Stuff from Samir

|<-------RR-Assignment Cmd (SDCCH)-------|          From Samir          | 
|<----------FACCH, L2 establish----------|          From Samir          | 
FACCH, RR-Assignment complete->
|<----------------CONNECT----------------|<-------Status: 200 OK--------| 
|-------------CONNECT ACK.-------------->|                              | 
|<-------------RTP traffic-------------->|<--------GSM traffic--------->| 

======== Call Hold 3GPP 4.83 2 and 4.80 sec 9.3.  SIP procedures in 24.228 10.1
== MO-Hold
|            Stop media flow             |                              | 
|-----------------Hold------------------>|                              | 
|                                        |--------UPDATE(HOLD)--------->| 
|                                        |       Stop media flow        | 
|                                        |<-------------OK--------------| 
|<------------HoldAcknowledge------------|                              | 
|<--------------HoldReject---------------|                              | 
== Notification of held party
|                                        |<--------UPDATE(HOLD)---------| 
|<------------Facility(Hold)-------------|-------------OK?------------->| 
== Retrieval of Held call (using the same TI of original held call)
|---------------Retrieve---------------->|                              | 
|                                        |-------UPDATE(Resume)-------->| 
|                                        |      Resume media flow       | 
|                                        |<-------------OK--------------| 
|<----------RetrieveAcknowledge----------|                              | 
|           Resume media flow            |                              | 
|<------------RetrieveReject-------------|                              | 
== Notification of held party
|                                        |<--------UPDATE(HOLD)---------| 
|<------------Facility(Hold)-------------|                              | 
== Switch
|-------------Hold(TI A-B)-------------->|             ...              | 
|-----------Retrieve(TI A-C)------------>|                              | 
|<--------HoldAcknowledge(TI A-B)--------|                              | 
|<------RetreiveAcknowledge(TI A-C)------|                              | 

======== Call Waiting 3GPP 4.83 1 and messages in 4.80 sec 9.3:
Note: Can only have one held call at a time, so if call waiting arrives when there is a call held,
the MS must release the held call before accepting the new call.
|<----------------L3Setup----------------|                              | 
|        Signal Information IE #7        |                              | 
|-------------CallConfirmed------------->|                              | 
|             cause#17(busy)             |                              | 
|--------------L3Alerting--------------->|                              | 
== Notification of held party
|----------------L3Setup---------------->|                              | 
|<------------CallProceeding-------------|                              | 
|                                        |                              | 
== If the MS wants to put existing call on hold:
|-------------Hold(TI A-B)-------------->|                              | 
|<--------HoldAcknowledge(TI A-B)--------|                              | 
|-----------L3Connect(TI C-B)----------->|                              | 
|<---------L3ConnectAck(TI C-B)----------|                              | 
== If the MS wants to drop current call and accept waiting call:
Normal procedure to drop, then normal procedure to accept.
== Regsiter with network for Supplementary Service (eg Call Waiting) 4.83 1.4
|---------------Register---------------->|                              | 
|<------------ReleaseComplete------------|                              | 

======== Codec and Media flow negotiaion 24.228 10.3
|        Determine UE1 codec set         |                              | 
|                                        |----------re-INVITE---------->| 
|                                        |Determine subset of code suppo| 
|                                        |<----183 Session Progress-----| 
|        Determine Initial codec         |                              | 
|                                        |------------PRACK------------>| 
|                                        |<-------------OK--------------| 
|      stop sending with old codec       |reserve resource for new codec| 
|                                        |-----------UPDATE------------>| 
|                                        |<-------------OK--------------| 
|                                        |<-----------Ringing-----------| 
|                                        |------PRACK(optional)??------>| 
|                                        |<------200 OK (to PRACK)------| 
|                                        | stop sending with old codec  | 
|                                        |              OK              | 
|      start sending with new codec      |                              | 
|                                        |-------------ACK------------->| 
|                                        | start sending with new codec | 

=== HANDOVER: pre-existing peer based.  This is based on Dougs diagrams.
MS                                      BS1                            BS2
|---------RR MeasurementReport---------->|                              | 
|                                        |--------REQ HANDOVER--------->| BS2:PeerInterface::processHandoverRequest
|                                        |<--------RSP HANDOVER---------| BS1:PeerInterface::processHandoverResponse
|<----------RR HandoverCommand-----------|                              |
|-------------------------RR  HandoverAccess-------------------------->>| 
|<<-----------------------RR PhysicalInformation------------------------| 
|-------------------------RR HandoverComplete------------------------->>| 
|                                        |                              | --------- re-INVITE ------->
|                                        |                              | <-------- OK ------------
|                                        |                              | --------- ACK ---------->
|                                        |----IND HANDOVER_COMPLETE---->| BS2:PeerInterface::processHandoverComplete
|                                        |<----ACK HANDOVER_COMPLETE----| 

=== HANDOVER: post-L3-Rewrite Peer Based Handover.
WIDTH 3|30|35
MS                            BS1                                 BS2
A |----RR MeasurementReport----->|                                   | 
  |                              |-----------REQ HANDOVER----------->| BS2:processHandoverRequest
  |                              |        (INFO with params)         | Note: transfers both BSS->BSS and MSC->MSC info.
  |                              |           (SIP REFER)             | 
B |                              |<-----------RSP HANDOVER-----------| BS1:processHandoverResponse
  |                              |  (200 OK with L3HandoverCommand)  |
  |                              |         (or 4xx failure)          |
  |<-----RR HandoverCommand------|                                   |
  |-----------------------RR  HandoverAccess----------------------->>| 
  |<<---------------------RR PhysicalInformation---------------------| 
  |----------------------RR HandoverComplete----------------------->>| 
C |                              |<-----IND HANDOVER_COMPLETE--------| BS1:processHandoverComplete
D |                              |-------ACK HANDOVER_COMPLETE------>| 
  |                              |           (SIP REFER)             | 
  |                              |                                   | ----- re-INVITE ----->
  |                              |                                   | <------- OK ----------
  |                              |                                   | -------- ACK -------->

=== HANDOVER using MAP.
HANDOVER using MAP from BSC perspective
See 08.08 3.1.5 External Handover and section 6 Figure 4,5,6,13,16
WIDTH 0|30|25|30
MS                            BS1                       BS2                            MSC
|----L3 MeasurementReport----->|                         |                              | 
|                              |-------------------HandoverRequired------------------->>|
|                              |(with cellid+transparent container)|
|                              |                         |<-------HandoverRequest-------|
|                              |                         | (with transparent container) |
|                              |                         |-----HandoverRequestAck------>|
|                              |                         |   (with L3HandoverCommand)   |
|                              |<<-------------------HandoverCommand--------------------|
|                              |(with L3HandoverCommand) |
|<------L3HandoverCommand------|                         |                              |
|------------------RR HandoverAccess------------------->>|                              |
|                              |                         |-------HandoverDetect-------->|
|<<----------------L3 PhysicalInformation----------------|                              |
|-----------------L3 HandoverComplete------------------>>|                              |
|                              |                         |------HandoverComplete------->|
|                              |<<---------------------ClearCommand---------------------|
|                              |--------------------ClearComplete--------------------->>|
FAILURE due to BS2 congestion:
|                              |                         |<-------HandoverRequest-------|
|                              |                         |-------HandoverFailure------->|
FAILURE to handover, interference:
|                              |<<-------------------HandoverCommand--------------------|
|                              |-------------------HandoverFailure-------------------->>|
Support for autonomous handover BS1->BS2, subsequently notify MSC:
|                              |                         |------HandoverPerformed------>|


WIDTH 0|30|30|30
HANDOVER using MAP from MSC perspective
Initial MSC to MSC Handover.  This picture omits the MS messages.  3GPP 23.009 7.1
BS1                          MSC-A                           MSC-B                          BS2     VLR-B
|-----BSSMAP-HO-Required------>|                              |                              |
|                              |---MAP-Prepare-HO-Required--->|                              |
|                              |  (includes HO-Required msg)  |                              |
|                              |                              |---MAP-AllocateHandoverNumberRequest -->
|                              |                              |------BSSMAP-HO-Request------>|
|                              |                              |<----BSSMAP-HO-RequestAck-----|
|                              |                              |<---MAP-SendHandoverReportRequest ------
|                              |<---MAP-Prepare-HO-Response---|                              |
|                              |-----------SS7 IAM----------->|                              |
|                              |                              |---MAP-SendHandoverReportResponse ---->
|                              |<-----------SS7 ACM-----------|                              |
|<------BSSMAP-HO-Command------|                              |                              |
|                              |                              |<----------HO-Detect----------|
|                              |<MAP-ProcessAccessSignalRequest|                              |
|                              |                              |<---------HO-Complete---------|
|                              |<--MAP-SendEndSignalRequest---|                              |
|<-----BSSMAP-ClearCommand-----|                              |                              |
|----BSSMAP-ClearComplete----->|                              |                              |
|                              |<---------SS7 ANSWER----------|                              |
Subsequent Handover from MSC-B to MSC-B' is the same as above except that:
	All MAP messages from MSC-B to MSC-B' are relayed through MSC-A.
	The SS7 IAM, ACM, etc go through MSC-A too.  I dont understand this.
	MAP-PrepareSubsequentHandoverRequest replaces MAP-PrepareHandove-Request
	MAP-PrepareSubsequentHandoverResponse replaces MAP-PrepareHandoverResponse


Messages:
3.2.1.9 HandoverRequired(with transparent container)  BS1->MSC
	3.2.1.7 HandoverRequest(with transparent container) BS2<-MSC
		Note: HandoverRequest identifies cells by by CellIdentity or LAI+CellIdentity.
		CellIdentity as defined in 8.08 BSSMAP documentation 3.2.2.27 refers to a non-existent reference in 4.08
		CellIdentity appears to be what the MS returns, 4.08/44.018 10.5.2.2, ie BCCH-ARFCN-C0+BCC+NCC
	HandoverRequestAck (with L3 HandoverCommand) BS2->MSC
	or: HandoverFailure BS2->MSC

3.2.1.11 HandoverCommand (with L3 HandoverCommand) BS1<-MSC
or: 3.2.1.37 HandoverRequiredReject(with cause) BS1<-MSC

	3.2.1.40 HandoverDetect BS2->MSC
	3.2.1.11 HandoverComplete BS2->MSC

HandoverFailure BS1->MSC	3.1.5.3.2: BS1 sends this if it detects that handover failed, ie, the MS returns and sends HandoverFailure.
	ClearCommand with cause "Radio interface failure, reversion to old channel" BS2<-MSC
	ClearComplete BS2->MSC
NO: 3.2.1.13 HandoverSucceeded BS1<-MSC  Used for group calls instead of ClearCommand
ClearCommand with cause "Handover Successful" BS1<-MSC
ClearComplete BS1->MSC

HandoverPerformed BS->MSC
	Message is sent for inter-cell handover procedure to indicate new cell MS is on.
	This is congruent with an OpenBTS->OpenBTS handover.

We can ignore:
	# These messages bracket a group of multiple HandoverRequired for MSC initiated handovers, see 3.1.8.
	3.2.1.14 HandoverCandidateEnquire BS1<-MSC
	3.2.1.15 HandoverCandidateResponse BS1->MSC
	HandoverCandidateEnquiry BS1<-MSC
	HandoverCandidateResponse BS1->MSC

BS1 -HandoverRequired->MSC
# MS                                      BS1                            BS2                        YATE
#|--------- RR MeasurementReport--------->|                              |                          |
#|                                        |--------------- BSSMAP HandoverRequest------------------>|
#|                                        |                              |<- BSSMAP Handover--------------- BSSMAP HandoverRequest------------------>|
#|                                        |



======== Session Redirection 24.228 10.4 with 4 sub-cases
==	Session Forward Unconditional
This is done by the network, and the ladder diagram is the same, but the alternate
destination is returned in the SIP 183 SessionProgress message
==	Session Forward No Answer
Instead of 183 SessionProgress the network returns 302 Moved Temporarily, then the UE retries through CS domain.
This is for IP based multimedia services.  How do we get the new address to an MS?
==	Session Forward Variable
Instead of 183 SessionProgress the network returns 302 Redirect
==	Session Forward Busy

WIDTH 0|40|30|
== Layer 3 Authentication
|          Look up Kc by imsi.           |                              | 
|<-GMM-AuthenticationAndCipheringRequest-|                              | 
|-GMM-AuthenticaionAndCipheringResponse->|                              | 

== Layer 3 ==
Simplest case: OpenBTS is a single Location Area and Routing Area.
Class-A or Class-B MS:
|----------GMM-AttachRequest,----------->|                              | 
|           MobileID, MS Caps            |                              | 
If MobileID is an unrecognized MCC+MNC+LAI+RAI+P-TMSI, then:
|<-----GMM-IdentityRequest for IMSI------|                              | 
|-------GMM-IdentityResponse,IMSI------->|                              | 
Note: The above establishes a database in the SGSN of mobileID to IMSI
Later:
|-----------GMM-AttachRequest----------->|                              | 
|         MobileID is recognized         |                              | 
|<-------Optional Authentication-------->|                              | 
|<-------GMM-AttachAccept, P-TMSI--------|                              | 
|----------GMM-AttachComplete----------->|                              | 
|New P-TMSI replaces previous, old P-TMS |                              | 


When MS changes Routing Area or Periodic timer expires:
|-----GMM-RoutingAreaUpdateRequest------>|                              | 
|      If MobileID not recognized:       |                              | 
|<------GMM-RoutingAreaUpdateReject------|                              | 
|        If MobileId recognized:         |                              | 
|<------GMM-RoutingAreaUpdateAccept------|                              | 

How it should work:
If the MS is in GMM-STANDBY state, it does an RA Update when it enters a new Routing Area.
|-----GMM-RoutingAreaUpdateRequest------>|                              | 
If MobileID not recognized by this SGSN, and we can determine old SGSN, ask old SGSN
and perform a PS handover.  In our current system GGSN is integrated so this may include PDP Context Info.

If the MS is in GMM-READY state, MS does something (what? maybe any layer 2 activity at all?)
when it enters a new cell, and we do a PS handover from the old SGSN.

Methods to find old-SGSN, no SGSN pool:
	o SGSN sever number can be encoded in the P-TMSI.
	o query a central server with the old P-TMSI.
	o include SGSN address in P-TMSI signature, which is only 24 bits.
Methods to find old-SGSN with SGSN pool:
	o Pool is established at startup.

== Suspension Request
== Detach

|     find or allocate P-TMSI when?      |                              | 

=======================  REGISTRATION ===========================

WIDTH 3|46
Standard registration messages
BTS                                      REGISTRAR
R1 |-------------REGISTER with IMSI-------------->|
A1 |<--------------200 OK with IMSI---------------| IMSI recognized and pre-authorized, no TMSI assignment.
F1 |<---------401 Unauthorized with IMSI----------| Permanent fail.  Unspecified if IMSI recognized or not.
F2 |<------401 Unauthorized with IMSI, nonce------| authorization request
R2 |----------REGISTER with IMSI, SRES----------->| standard response to F2.

New Registration messages to support TMSI:
R3 |------------REGISTER with oldTMSI------------>| Initial Register by TMSI.
F2 |<-401 Unauthorized with oldTMSI, IMSI, nonce--| authorization request
A2 |<----------200 OK with IMSI, oldTMSI----------| authorized, TMSI ok.
A3 |<----------200 OK with IMSI, newTMSI----------| authorized, TMSI reassignment requested.
F3 |<---------485 Ambiguous with oldTMSI----------| TMSI not recognized or ambiguous.  BTS must query for IMSI.
R4 |---------REGISTER with oldTMSI, SRES--------->| response to F2.
R5 |---------REGISTER with IMSI, oldTMSI--------->| response to F3: 485 Ambiguous
R6 |---------REGISTER with IMSI, newTMSI--------->| response to A3: TMSI reassignment confirmation.


Notes:
Reply A1 is the standard Registrar reply, but will not be used by Yate-MSC.
Reply A2 and A3 are distguinshable by the BTS by whether the returned TMSI matches the existing or not.
Reply A3 should generally not be used to authorize an LUR using tmsi without challenge, however,
this is currently how follow-on requests are handled, ie, after LUR,
a CMServiceRequest with TMSI proceeds without challenge.

The TMSI will be indicated by "TMSI" followed by exactly 8 hex digits, case irrelevant.  When allocating TMSIs It is slightly preferable that the top two bits of the TMSI be invariant, for example, two 0 bits.
When TMSI and IMSI are both included in a SIP message, one of them will be put in a new header field.
I do nto think it is adisable to use any existing field names that proxies may recognize.
I suggest the header name: "P-Alt-Id".

Registration with IMEI:
In addition, any REGISTER message may include IMEI.
Since a customer may not want IMEI sent in clear any more than IMSI sent in clear,
I believe this should be an option controlled by the MSC.
To implement that a flag needs to be sent from the REGISTRAR to the BTS, and the BTS will respond
by returning the IMEI in the next message.
I suggest the header name: "P-IMEI".  If present in a reply from the registrar it is a request for IMEI.
The BTS will return the IMEI, which is in decimal, prefixed by "IMEI", which is slightly verbose.
Normally the Registrar would include P-IMEI in the 401 or 485 replies.
The BTS will not automatically query the IMEI before sending the initial REGISTER message.

Next Topic: TMSI Caching.
Under what circumstances should the BTS cache the TMSI, and for how long, and will it work in UMTS?
TMSI caching is meant to allow the BTS to respond to an MS request without requesting authorization from the Registrar.
Possible reasons include:
To reduce network traffic.
To allow the BTS to be used during temporary loss of contact with the Registrar.
Currently OpenBTS requires a IMSI for LUR but allows a subsequent CMServiceRequest by TMSI.
Currently OpenBTS caches TMSIs forever.  Note that the BTS is not informed when an MS leaves the cell,
although a powered MS will continue to perform periodic LUR every hour.

Reasons not to cache TMSIs:
A UMTS UE with a U-SIM will refuse to connect unless it is challenged.
I did not look up the exact circumstances, but there is no value in TMSI caching when
we need to contact the Registrar for new authenticaion anyway. 
We have also talked about caching multiple nonce.
The TMSI may collide with an existing TMSI.  Performing authorization is a way to detect this.
For this reason we authorize MS by TMSI even when using open-registration.

I suggest that reducing network traffic is hardly worth the effort at this point.
Here are the possibilities as I see it:
1.  Punt.  The BTS always contacts the MSC every time the MS establishes a new connection.  This means the only
way a GSM CMServiceRequest would not require a new authorization is if it immediately follows LUR as a follow-on request.
2.  Have an option that allows CMServiceRequest without authorization for GSM MS that have had a successful LUR within the hour.
3.  Put a lifetime on the TMSI returned by the Registrar.  This needs to be returned by the MSC, not be an OpenBTS option, because it will depend on whether the SIM is a U-SIM.


How to configure OpenBTS.
Two sets of options, one for normal use and one for loss of contact with Registrar.
Service:
	LUR, CS, SMS, GPRS.
For OpenRegistration:
	Allow TMSI without authorization.
	Allow TMSI with authorization using cached nonce.
	Options are: allow with OpenRegistration. 
I suggest that OpenRegistration 

THE END
EOF
