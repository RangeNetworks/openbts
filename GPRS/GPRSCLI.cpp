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

#include "GPRSInternal.h"
#include "TBF.h"
#include "RLCEngine.h"
#include "MAC.h"
#include "FEC.h"
#include "RLCMessages.h"
#include "Interthread.h"
#include "BSSG.h"
#include "LLC.h"
#define strmatch(what,pat) (0==strncmp(what,pat,strlen(pat)))

using namespace BSSG;
using namespace SGSN;

#define BAD_NUM_ARGS 1	// See CLI/CLI.cpp

#define RN_CMD_OPTION(opt) (argi<argc && 0==strcmp(argv[argi],opt) ? ++argi : 0)
#define RN_CMD_ARG (argi<argc ? argv[argi++] : NULL)
//#define RN_CMD_OPTION(o) (argc>1 && 0==strncmp(argv[1],o,strlen(o)) ? argc--,argv++,1 : 0)

namespace GPRS {


static int gprsMem(int argc, char **argv, int argi, ostream&os)
{
	gMemStats.text(os);
	return 0;
}

static void printChans(bool verbose, ostream&os)
{
	PDCHL1FEC *pch;
	RN_MAC_FOR_ALL_PDCH(pch) { pch->mchDump(os,verbose); }
}

//static int gprsChans(int argc, char **argv, int argi, ostream&os)
//{
//	bool verbose=0;
//	while (argi < argc) {
//		if (strmatch(argv[argi],"-v")) { verbose = 1; argi++; continue; }
//		os << "oops! unrecognized arg:" << argv[argi] << "\n";
//		return 0;
//	}
//	printChans(verbose,os);
//	return 0;
//}

static int gprsList(int argc, char **argv, int argi, ostream&os)
{
	bool xflag=0, aflag=0, listms=0, listtbf=0, listch=0;
	int options = 0;
	int id = -1;
	while (argi < argc) {
		if (strmatch(argv[argi],"ch")) { listch = 1; argi++; continue; }
		if (strmatch(argv[argi],"tbf")) { listtbf = 1; argi++; continue; }
		if (strmatch(argv[argi],"ms")) { listms = 1; argi++; continue; }
		if (strmatch(argv[argi],"-v")) { options |= printVerbose; argi++; continue; }
		if (strmatch(argv[argi],"-c")) { options |= printCaps; argi++; continue; }
		if (strmatch(argv[argi],"-x")) { xflag = 1; argi++; continue; }
		if (strmatch(argv[argi],"-a")) { aflag = 1; argi++; continue; }
		if (isdigit(argv[argi][0])) {
			if (id >= 0) goto oops;  // already found a number
			id = atoi(argv[argi]);
			argi++;
			continue;
		}
		oops:
		os << "oops! unrecognized arg:" << argv[argi] << "\n";
		return 0;
	}

	bool all = !(listch|listtbf|listms);

	if (all|listms) {
		MSInfo *ms;
		for (RListIterator<MSInfo*> itr(xflag ? gL2MAC.macExpiredMSs : gL2MAC.macMSs); itr.next(ms); ) {
			if (id>=0 && (int)ms->msDebugId != id) continue;
			if (aflag || ! ms->msDeprecated) { ms->msDump(os,(PrintOptions)options); }
		}
	}
	if (all|listtbf) {
		TBF *tbf;
		//RN_MAC_FOR_ALL_TBF(tbf) { os << tbf->tbfDump(verbose); }
		for (RListIterator<TBF*> itr(xflag ? gL2MAC.macExpiredTBFs : gL2MAC.macTBFs); itr.next(tbf); ) {
			// If the id matches a MS, print the TBFs associated with that MS.
			if (id>=0 && (int)tbf->mtDebugId != id && (int)tbf->mtMS->msDebugId != id) continue;
			os << tbf->tbfDump(options&printVerbose) << endl;
		}
	}
	if (all|listch) {
		printChans(options&printVerbose,os);
	}
	return 0;
}

static int gprsFree(int argc, char **argv, int argi, ostream&os)
{
	char *what = RN_CMD_ARG;
	char *idstr = RN_CMD_ARG;
	if (!idstr) return BAD_NUM_ARGS;
	int id = atoi(idstr);
	MSInfo *ms;  TBF *tbf;
	if (strmatch(what,"ms")) {
		RN_MAC_FOR_ALL_MS(ms) {
			if (ms->msDebugId == (unsigned)id) {
				os << "Deleting " <<ms <<"\n";
				ms->msDelete(1);
				return 0;
			}
		}
		os << "MS# "<<id <<" not found.\n";
	} else if (strmatch(what,"tbf")) {
		RN_MAC_FOR_ALL_TBF(tbf) {
			if (tbf->mtDebugId == (unsigned)id) {
				os << "Deleting " <<tbf <<"\n";
				tbf->mtDelete(1);
				return 0;
			}
		}
		os << "TBF# "<<id <<" not found.\n";
	} else if (strmatch(what,"ch")) {
		// They dont have an id.  Just delete the nth one.
		PDCHL1FEC*pch;
		RN_MAC_FOR_ALL_PDCH(pch) {
			if (id-- == 0) {
				os << "Deleting " <<pch <<"\n";
				delete pch;
				return 0;
			}
		}
		os << "Channel not found; use 0, 1, 2, etc for the Nth channel in gprs list ch\n";
	} else {
		os << "gprs free: unrecognized argument: " << what << "\n";
	}
	return 0;
}

static int gprsFreeExpired(int argc, char **argv, int argi, ostream&os)
{
	MSInfo *ms;
	for (RListIterator<MSInfo*> itr(gL2MAC.macExpiredMSs); itr.next(ms); ) {
		itr.erase();
		delete ms;
	}
	TBF *tbf;
	for (RListIterator<TBF*> itr(gL2MAC.macExpiredTBFs); itr.next(tbf); ) {
		itr.erase();
		delete tbf;
	}
	return 0;
}

static int gprsStats(int argc, char **argv, int argi, ostream&os)
{
	if (!GPRSConfig::IsEnabled()) {
		os << "GPRS is not enabled.  See 'GPRS.Enable' option.\n";
		return 0;
	}
	GSM::Time now = gBTS.time();
	os << "GSM FN=" << now.FN() << " GPRS BSN=" << gBSNNext << "\n"; 
	os << "Current number of"
		<< " PDCH=" << gL2MAC.macPDCHs.size()
		<< " MS=" << gL2MAC.macMSs.size()
		<< " TBF=" << gL2MAC.macTBFs.size()
		<< "\n";
	os << "Total number of"
		<< " PDCH=" << Stats.countPDCH
		<< " MS=" << Stats.countMSInfo
		<< " TBF=" << Stats.countTBF
		<< " RACH=" << Stats.countRach
		<< "\n";
	os << "Downlink utilization=" << gL2MAC.macDownlinkUtilization << "\n";
	os << LOGVAR2("ServiceLoopTime",Stats.macServiceLoopTime) << "\n";
	return 0;
}

#if 0	// pinghttp test code not linked in yet.
static int gprsPingHttp(int argc, char **argv, int argi, ostream&os)
{
	if (argi >= argc) { os << "syntax: gprs pinghttp address\n"; return 1; }
	//char *addr = argv[argi++];
	os << "pinghttp unimplemented\n";
	return 0;
}
#endif

// Start the service and allocate a channel.
// This is redundant - can call rach.
static int gprsStart(int argc, char **argv, int argi, ostream&os)
{
	// Start the thread, if not running.
	char *modearg = RN_CMD_ARG;
	if (modearg) {
		gL2MAC.macSingleStepMode = strmatch(modearg,"s");
		if (!gL2MAC.macSingleStepMode) { os << "Unrecognized arg: "<<modearg<<"\n"; return 0; }
	} else {
		gL2MAC.macSingleStepMode = 0;
	}

	// Reinit the config in case single step mode is changing.
	gL2MAC.macConfigInit();

	if (gL2MAC.macRunning) {
		os << "gprs service thread already running.\n";
	} else if (gL2MAC.macStart()) {
		os << "started gprs service "
				<<(gL2MAC.macSingleStepMode ? "single step mode" : "thread") << "\n";
	} else {
		os << "failed to start gprs service.\n";
	}

	// The macAddChannel below is failing.  Try waiting a while.
	// Update: it fails between the time OpenBTS first starts up and the transceiver times out,
	// about a 5 second window after start up.  This may only happen after a crash.
	// Update again: I think on startup the channels come up with the recyclable timers
	// running and we cannot allocate a channel until they expire.
	sleep(1);

	// Allocate a channel, if none already allocated.
	if (! gL2MAC.macActiveChannels()) { gL2MAC.macAddChannel(); }
	if (! gL2MAC.macActiveChannels()) {
		os << "failed to allocate channel for gprs.\n";
	} else {
		PDCHL1FEC *pdch = gL2MAC.macPickChannel();
		assert(pdch);
		os << "allocated channel for gprs: " << pdch << "\n";
	}
	return 0;
}

static int gprsStop(int argc, char **argv, int argi, ostream&os)
{
	bool cflag = RN_CMD_OPTION("-c");
	gL2MAC.macStop(cflag);
	return 0;
}

static int gprsTestRach(int argc, char **argv, int argi, ostream&os)
{
	GSM::Time now = gBTS.time();
	unsigned RA = 15 << 5;
	GPRSProcessRACH(RA,now,-20,0.5);
	return 0;
}

extern "C" {
	int gprs_llc_fcs(uint8_t *data, unsigned int len);
}

static int gprsTest(int argc, char **argv, int argi, ostream&os)
{
#if 0
	LLCParity parity;
	ByteVector bv;
	for (int i = 0; i <= 5; i++) {
		switch (i) {
		case 0: bv = ByteVector(3); bv.fill(0); bv.setByte(2,1); break;
		case 1: bv = ByteVector(3); bv.fill(0); bv.setByte(2,2); break;
		case 2: bv = ByteVector(3); bv.fill(0); bv.setByte(0,0x80); break;
		case 3: bv = ByteVector(3); bv.fill(0); bv.setByte(0,0x40); break;
		case 4: bv = ByteVector(3); bv.fill(0); bv.setByte(0,0); break;
		case 5: bv = ByteVector("Now is the time for all good men"); break;
		}
		unsigned crc1 = gprs_llc_fcs(bv.begin(),bv.size());
		unsigned crc2 = parity.computeFCS(bv);
		printf("size=%d, byte=0x%x crc=0x%x, crcnew=0x%x\n",bv.size(),bv.getByte(0),crc1,crc2);
	}
#endif

	//GPRSLOG(1) << "An unterminated string";
	//GPRSLOG(1) << "Another unterminated string";
	return 0;
}

// Try printing out all the RLC messages.
// For the small ones, we will just print them out to see if that functionality works.
// For most of the big ones, we will actually call the function that generates these messages.
static int gprsTestMsg(int argc, char **argv, int argi, ostream&os)
{
	if (!gL2MAC.macActiveChannels()) {
		os << "Oops!  You must allocate GPRS channels before running this test; try grps start.\n";
		return 0;
	}

	Time gsmfn = gBTS.clock().FN();
	os << "Test Messages, Current time:"<<LOGVAR(gsmfn)<<LOGVAR(gBSNNext)<<"\n";

	// Create a dummy RLCRawBLock for the uplink messages to parse.
	BitVector bvdummy(52*8);
	bvdummy.zero();
	RLCRawBlock rb(99,bvdummy,0,0,ChannelCodingCS1);

	// NOTE NOTE NOTE!  This debug code is running in a different thread
	// than the MAC service loop.  If you try to run the code below while
	// the MAC service loop is running, it will just crash.
	gL2MAC.macStop(0);

	// Create an MS, and some TBFs.
	PDCHL1FEC *pdch = gL2MAC.macPickChannel();	// needed for immediate assignment msg.
	MSInfo *ms = new MSInfo(123456);	// number is the TLLI.
	RLCDownEngine *downengine = new RLCDownEngine(ms);
	TBF *downtbf = downengine->getTBF();
	RLCUpEngine *upengine = new RLCUpEngine(ms,0);
	TBF *uptbf = upengine->getTBF();
	uptbf->mtAttach();		// Assigns USF and TFI for the tbf, so we can see it in the messages.
	downtbf->mtAttach();
	os << "uplink TBF for messages is:\n";
	os << uptbf->tbfDump(1);
	os << "downlink TBF for messages is:\n";
	os << downtbf->tbfDump(1);

	os << "struct RLCMsgPacketUplinkDummyControlBlock : public RLCUplinkMessage\n";
	struct RLCMsgPacketUplinkDummyControlBlock msgupdummy(&rb);
	msgupdummy.text(os);

	os << "\n\nstruct RLCMsgPacketDownlinkAckNack : public RLCUplinkMessage\n";
	RLCMsgPacketDownlinkAckNack msgdownacknack(&rb);
	msgdownacknack.text(os);

	os << "\n\nstruct RLCMsgPacketControlAcknowledgement : public RLCUplinkMessage\n";
	RLCMsgPacketControlAcknowledgement msgcontrolack(&rb);
	msgcontrolack.text(os);

	os << "\n\nstruct RLCMsgPacketResourceRequest : public RLCUplinkMessage\n";
	RLCMsgPacketResourceRequest resreq(&rb);
	resreq.text(os);

	os << "\n\nclass RLCMsgPacketAccessReject : public RLCDownlinkMessage\n";
	RLCMsgPacketAccessReject msgreject(downtbf);
	msgreject.text(os);

	os << "\n\nclass RLCMsgPacketTBFRelease : public RLCDownlinkMessage\n";
	RLCMsgPacketTBFRelease msgtbfrel(downtbf);
	msgtbfrel.text(os);

	os << "\n\nclass RLCMsgPacketUplinkAckNack : public RLCDownlinkMessage\n";
	RLCMsgPacketUplinkAckNack *msgupacknack = upengine->engineUpAckNack();
	msgupacknack->text(os);
	delete msgupacknack;

	os << "\n\nstruct RLCMsgPacketDownlinkDummyControlBlock : public RLCDownlinkMessage\n";
	RLCMsgPacketDownlinkDummyControlBlock msgdowndummy;
	msgdowndummy.text(os);

	os << "\n\nL3ImmediateAssignment for Single Block Packet Assignment\n";
	//ms->msMode = RROperatingMode::PacketIdle;
	sendAssignment(pdch,downtbf, &os);

	os << "\n\nclass RLCMsgPacketDownlinkAssignment : public RLCDownlinkMessage\n";
	//ms->msMode = RROperatingMode::PacketTransfer;
	ms->msT3193.set();
	// To force the MS to send the message on PACH we can set T3191
	sendAssignment(pdch,downtbf, &os);
	ms->msT3193.reset();

	os << "\n\nclass RLCMsgPacketUplinkAssignment : public RLCDownlinkMessage\n";
	sendAssignment(pdch,uptbf, &os);

	return 0;
}

static int gprsTestBSN(int argc, char **argv, int argi, ostream&os)
{
	RLCBSN_t bsn = 0;
	int fn = 0;
	for (fn = 0; fn < 100; fn++) {
		bsn = FrameNumber2BSN(fn);
		int fn2 = BSN2FrameNumber(bsn);
		os << LOGVAR(fn) <<LOGVAR(bsn) <<LOGVAR(fn2) <<"\n";
	}
	return 0;
}

// Create an uplink block as would be sent by an MS.
// The payload in sequential blocks will be ascending 16-bit words.
// Ie, payload of first block is 0,1,2,3,4,5,6,7,8,9
// and of second block is 10,11,12,13,14,15,16,17,18,19, etc.
// Makes it easy to check if the final assembled PDU is correct.
// TODO: We are not testing a partial final block.
static RLCRawBlock *fakeablock(int bsn, int tfi, int final)
{
	// Uplink always uses CS1.
	// The payload size is 20 bytes, but this is where it comes from.
	// We are going to assume it is even so we can just fill the payload
	// with sequential integers.
	int payloadsize = RLCPayloadSizeInBytes[ChannelCodingCS1];

	// Create the uplink block header structure, which we can write into the bitvector.
	RLCUplinkDataBlockHeader bh;
	bh.mmac.mPayloadType = MACPayloadType::RLCData;
	bh.mmac.mCountDownValue = final ? 0 : 15;	// countdown, just use 15 until final block.
	bh.mmac.mSI = 0;	// stall indicator from MS.
	bh.mmac.mR = 0;		// retry bit from MS.
	bh.mSpare = 0;		// Doesnt matter.
	bh.mPI = 0;			// We dont use this.
	bh.mTFI = tfi;
	bh.mTI = 0;			// We dont use this.
	// Octet 2: (starts at bit 16)
	bh.mBSN = bsn;
	bh.mE = 1;	// Extension bit: 1 means whole block is data

	// Create a bitvector with an image of the header above followed by the
	// data, which will just be sequential integers starting at bsn.
	// Size is 1 byte MAC header + 2 bytes RLC header + payload
	BitVector vec(8*(1 + 2 + payloadsize));

	// Create a MsgCommon BitVector writer:
	MsgCommonWrite mcw(vec);

	// Write the header.
	bh.mmac.writeMACHeader(mcw);
	bh.writeRLCHeader(mcw);

	// Write the payload.
	int numwords = payloadsize/2;
	uint16_t dataword = bsn * numwords;	// Starting payload word for this bsn.
	for ( ; numwords > 0; numwords--) {
		mcw.writeField(dataword++,16); // Write as a 16 bit word.
	}

	// The BitVector now looks like something the MS would send us.
	// Go through the steps GPRS code uses to parse the incoming BitVector:

	// The GSM radio queues us an RLCRawBlock:
	RLCRawBlock *rawblock = new RLCRawBlock(bsn,vec,0,0,ChannelCodingCS1);

	return rawblock;
}

#if INTERNAL_SGSN==0
static int gprsTestUl(int argc, char **argv, int argi, ostream&os)
{
	bool randomize = RN_CMD_OPTION("-r");
	int32_t mytlli = 6789;
	MSInfo *ms = new MSInfo(mytlli);
	RLCUpEngine *upengine = new RLCUpEngine(ms,0);
	TBF *uptbf = upengine->getTBF();

	InterthreadQueue<NSMsg> testQ;
	gBSSG.mbsTestQ = &testQ;	// Put uplink blocks on our own queue.

	int payloadsize = RLCPayloadSizeInBytes[ChannelCodingCS1]; // 20 bytes / block.
	int numbytes = 200;			// Max PDU size is 1500; we will test 20*20 == 400 to start.
	int numblocks = numbytes / payloadsize;
	// The window size is only 64, so we would normlly have to wait for the ack before proceeding.
	// If we single stepped the MAC service loop while doing this, the dlservice routine
	// would do that.
	int bsn;
	int TFI = 1; // Use a fake tfi for this test.
	for (int j = 0; j < numblocks; j++) {
		if (randomize && j < numblocks-16) {
			// Goof up the order to see if blocks are reassembled in proper order.
			// I left the last few blocks alone to simplify.
			bsn = (j & 0xffff0) + ~(j & 0xf);
		} else {
			bsn = j;
		}
		int final = bsn == numblocks-1;
		RLCRawBlock *rawblock = fakeablock(bsn,TFI,final);
		// Raw uplink blocks are dequeued by processRLCUplinkDataBlock which
		// sends them to the uplink engine thusly:
		RLCUplinkDataBlock *rb = new RLCUplinkDataBlock(rawblock);
		//rb->text(os);
		// TODO: call processRLCUplinkDataBlock to test tfis
		delete rawblock;
		uptbf->engineRecvDataBlock(rb,0);
		// The final block has E bit set, which makes the RLCUpEngine call sendPDU(),
		// which sends the blocks to the BSSG, which puts them on our own queue.
	}
	gBSSG.mbsTestQ = NULL;	// Restore BSSG to normal use.

	// Examine results on the testq.Get the block from the BSSG transmit queue.
	if (testQ.size() != 1) {
		os << "Unexpected BSSG Queue size="<<testQ.size()<<"\n";
		return 0;	// We might be leaving some memory unfreed.
	}
	
	// ulmsg is a BSSGMsgULUnitData.
	NSMsg *msg = testQ.read();
	BSSGMsgULUnitData *ulmsg = (BSSGMsgULUnitData*)msg;

	// Check some things in the header.
	int expected = mytlli;
	if (ulmsg->getTLLI() != expected) {
		os << "ULUnitData msg wrong tlli=" << ulmsg->getTLLI() <<LOGVAR(expected)<<"\n";
	}

	BSSGMsgULUnitDataHeader *ulhdr = ulmsg->getHeader();
	expected = BSPDUType::UL_UNITDATA;
	if (ulhdr->mbuPDUType != expected) {
		os << "ULUnitData msg wrong PDUType=" << ulhdr->mbuPDUType <<LOGVAR(expected)<<"\n";
	}

	int hdrsize = BSSGMsgULUnitData::HeaderLength;
	if (0) {
		// This test is incorrect: the output byte vector is always allocated max size
		// so it does not have to be grown.
		int msgsize = ulmsg->size();
		expected = numbytes+hdrsize;
		if (msgsize != expected) {
			os << "BSSG UL UnitData msg wrong size" << LOGVAR(msgsize) << LOGVAR(expected)<<"\n";
		}
	}

	// Check the data:
	expected = 0;
	int offset;
	int bads = 0;
	for (offset = 0; offset < numbytes; offset += sizeof(short), expected++) {
		int got = ulmsg->getUInt16(hdrsize+offset);
		if (got != expected) {
			os << "BSSG data wrong at "<<LOGVAR(offset)<<LOGVAR(got)<<LOGVAR(expected)<<"\n";
			if (++bads > 10) break;
		}
	}

	ulmsg->text(os);
	/***
	os << "Data from beginning was:\n";
	todo: dump the ulmsg directly.  Use << this for ByteVector.
	offset = 0;
	for (int l = 0; l < 10; l++) {
		os << offset << ":";
		for (int i = 0; i < 10; i++, offset+=2) { os <<" " <<ulmsg->getUInt16(offset); }
		os << "\n";
	}
	***/

	delete ulmsg;
	return 0;
}
#endif


static int gprsDebug(int argc, char **argv, int argi, ostream&os)
{
	if (argi < argc) {
		int newval = strtol(argv[argi++],NULL,0); // strtol allows hex
		gConfig.set("GPRS.Debug",newval);
		GPRSSetDebug(newval);
	} else if (! GPRSDebug) {
		//GPRSSetDebug(3);
	}
	char buf[100]; sprintf(buf,"GPRSDebug=0x%x\n",GPRSDebug);
	os << buf;
	return 0;
}

static int gprsSet(int argc, char **argv, int argi, ostream&os)
{
	char *what = RN_CMD_ARG;
	if (!what) { return BAD_NUM_ARGS; }
	char *val = RN_CMD_ARG;		// may be null.

	if (strmatch(what,"clock") || strmatch(what,"sync")) {
		if (val) { gFixSyncUseClock = atoi(val); }
		os << LOGVAR(gFixSyncUseClock) << "\n";
	} else if (strmatch(what,"console")) {
		if (val) { gLogToConsole = atoi(val); }
		os << LOGVAR(gLogToConsole) << "\n";
	} else {
		os << "gprs set: unrecognized argument: " << what << "\n";
	}
	return 0;
}

static int gprsStep(int argc, char **argv, int argi, ostream&os)
{
	if (!gL2MAC.macSingleStepMode) {
		os << "error: MAC is not in single step mode\n";
		return 0;	// disaster would ensue if we accidently started another serviceloop.
	}
	// We single step it ignoring the global clock, which
	// might result in messages from the channel service routines.
	++gBSNNext;
	gL2MAC.macServiceLoop();
	return 0;
}

static int gprsConsole(int argc, char **argv, int argi, ostream&os)
{
	gLogToConsole = !gLogToConsole;		// Default: toggle.
	if (argi < argc) { gLogToConsole = atoi(argv[argi++]); }
	os << "LogToConsole=" << gLogToConsole << "\n";
	return 0;
}

static struct GprsSubCmds {
	const char *name;
	int (*subcmd)(int argc, char **argv, int argi,std::ostream&os);
	const char *syntax;
} gprsSubCmds[] = {
	{ "list",gprsList,	"list [ms|tbf|ch] [-v] [-x] [-c] [id]  # list active objects of specified type;\n\t\t -v => verbose; -c => include MS Capabilities -x => list expired rather than active" },
	{ "stat",gprsStats, "stat  # Show GPRS statistics" },
	{ "free",gprsFree, "free ms|tbf|ch id   # Delete something" },
	{ "freex",gprsFreeExpired, "freex	# free expired ms and tbf structs" },
	{ "debug",gprsDebug,	"debug [level]  # Set debug level; 0 turns off" },
	{ "start",gprsStart,	"start [step]   # Start gprs, optionally in single-step-mode;\n\t\t- can also start by 'gprs rach'" },
	{ "stop",gprsStop,	"stop [-c]  # stop gprs thread and if -c release channels" },
	{ "step",gprsStep,	"step    # single step the MAC service loop (requires 'start step')." },
	{ "set",gprsSet,	"set name [val]   # print and optionally set a variable - see source for names" },
	{ "rach",gprsTestRach,	"rach   # Simulate a RACH, which starts gprs service" },
	{ "testmsg",gprsTestMsg,	"testmsg   # Test message functions" },
	{ "testbsn",gprsTestBSN,	"testbsn   # Test bsn<->frame number functions" },
#if INTERNAL_SGSN==0
	{ "testul",gprsTestUl,	"testul [-r]   # Send a test PDU through the RLCEngine; -r => randomize order " },
#endif
	{ "console",gprsConsole, "console [0|1]  # Send messages to console as well as /var/log/OpenBTS.log;\n\t\t (default=1 for debugging)" },
	{ "mem",gprsMem,	"mem   # Memory leak detector - print numbers of structs in use" },
	{ "test",gprsTest,	"test   # Temporary test" },
	// Dont have the source code for pinghttp linked in yet.
	//{ "pinghttp",gprsPingHttp,"pinghttp address  # Send an http request to address (dont use google.com)" },
	// The "help" command is handled internally by gprsCLI.
	{ NULL,NULL }
};

static void help(std::ostream&os)
{
	os << "gprs sub-commands to control GPRS radio mode.  Syntax: gprs subcommand <options...>\n";
	os << "subcommands are:\n";
	struct GprsSubCmds *gscp;
	for (gscp = gprsSubCmds; gscp->name; gscp++) {
		os << "\t" << gscp->syntax;
		//if (gcp->arg) os << " " << gcp->arg;
		os << "\n";
	}
	os << "Notes:\n";
	os << "  Downlink utilization averaged over 5 seconds; 1.0 means full utilization;\n";
	os << "  2.0 means downlink requests exceeds available bandwidth by 2x, etc.\n";
}

// Set defaults for gprs debugging.
/*******
static void debugdefaults()
{
	static int inited = 0;
	if (!inited) {
		inited = 1;
		GPRSDebug = 3;
		gLogToConsole = 1;
	}
}
*******/

// Should return: SUCCESS (0), BAD_NUM_ARGS(1), BAD_VALUE(2), FAILURE (5)
// but sadly, these are defined in CLI.cpp, so I guess we just return 0.
// Note: argv includes command name so argc==1 implies no args.
int gprsCLI(int argc, char **argv, std::ostream&os)
{
	//debugdefaults();
	ScopedLock lock(gL2MAC.macLock);

	if (argc <= 1) { help(os); return 1; }
	int argi = 1;	// The number of arguments consumed so far; argv[0] was "gprs"
	char *subcmd = argv[argi++];

	struct GprsSubCmds *gscp;
	int status = 0;	// maybe success
	for (gscp = gprsSubCmds; gscp->name; gscp++) {
		if (0 == strcasecmp(subcmd,gscp->name)) {
			status = gscp->subcmd(argc,argv,argi,os);
			if (status == BAD_NUM_ARGS) {
				os << "wrong number of arguments\n";
			}
			return status;
			//if (gscp->arg == NULL || (argi < argc && 0 == strcasecmp(gscp->arg,argv[argi+1]))) {
				//gscp->subcmd(argc,argv,argi + (gscp->arg?1:0),os);
			//}
		}
	}

	if (strcasecmp(subcmd,"help")) {
		os << "gprs: unrecognized sub-command: "<<subcmd<<"\n";
		status = 2;	// bad command
	}
	help(os);
	return status;
}

};	// namespace
