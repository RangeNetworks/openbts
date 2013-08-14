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
#include "RLCMessages.h"
#include "RLCEngine.h"
#include "FEC.h"
#include "GSMTAPDump.h"
#include "../TransceiverRAD1/Transceiver.h"	// For Transceiver::IGPRS
#define FEC_DEBUG 0

namespace GPRS {
static BitVector *decodeLowSide(const RxBurst &inBurst, int B, GprsDecoder &decoder, ChannelCodingType *ccPtr);
//static int sFecDebug = 1;

//static Mutex testlock;	Did not help.

const int GPRSUSFEncoding[8] = {
	// from table at GSM05.03 sec 5.1.4.2, specified in octal.
	// 3 bits in, 12 bits out.
	// Note that the table is defined as encoding bits 0,1,2 of usf,
	// which is the post-byte-swapped version, not the original usf.
	00000, // 000 000 000 000
	00335, // 000 011 011 101
	01566, // 001 101 110 110
	01653, // 001 110 101 011
	06413, // 110 100 001 011
	06726, // 110 111 010 110
	07175, // 111 001 111 101
	07240  // 111 010 100 000
};


// Do the reverse encoding on usf, and return the reversed usf,
// ie, the returned usf is byte-swapped.
static int decodeUSF(SoftVector &mC)
{
	// TODO: Make this more robust.
	// Update: No dont bother, should always be zero anyway.
	return (mC.bit(0)<<2) | (mC.bit(6)<<1) | mC.bit(4);
}

ARFCNManager *PDCHCommon::getRadio() { return mchParent->mchOldFec->getRadio(); }
unsigned PDCHCommon::ARFCN() { return mchParent->mchOldFec->ARFCN(); }
unsigned PDCHCommon::CN() { return mchParent->mchOldFec->CN(); }
unsigned PDCHCommon::TN() { return mchParent->mchOldFec->TN(); }
PDCHL1Uplink *PDCHCommon::uplink() { return mchParent->mchUplink; }
PDCHL1Downlink *PDCHCommon::downlink() { return mchParent->mchDownlink; }
PDCHL1FEC *PDCHCommon::parent() { return mchParent; }
void PDCHL1FEC::debug_test() { /*printf("dt\n"); devassert(*mReservations[3]._vptr != NULL);*/ }
L1Decoder* PDCHCommon::getDecoder() const { return mchParent->mchOldFec->decoder(); }
void PDCHCommon::countGoodFrame() { getDecoder()->countGoodFrame(); }
void PDCHCommon::countBadFrame() { getDecoder()->countBadFrame(); }
float PDCHCommon::FER() const { return getDecoder()->FER(); }

// Print out the USFs bracketing bsn on either side.
const char *PDCHCommon::getAnsweringUsfText(char *buf,RLCBSN_t bsn)
{
	PDCHL1FEC *pdch = parent();
	sprintf(buf," AnsweringUsf=%d %d [%d] %d %d",
		pdch->getUsf(bsn-2),pdch->getUsf(bsn-1), pdch->getUsf(bsn),pdch->getUsf(bsn+1),pdch->getUsf(bsn+2));
	return buf;
}

// Must return true if ch1 is before ch2.
bool chCompareFunc(PDCHCommon*ch1, PDCHCommon*ch2)
{
	if (ch1->ARFCN() < ch2->ARFCN()) return true;
	if (ch1->ARFCN() > ch2->ARFCN()) return false;
	if (ch1->TN() < ch2->TN()) return true;
	return false;
}

void PDCHL1FEC::mchStart() {
	getRadio()->setSlot(TN(),Transceiver::IGPRS);
	// Load up the GPRS filler idle burst tables in the transceiver.
	// We could use any consecutive bsn, but lets use ones around the current time
	// just to make sure they get through in case someone is triaging somewhere.
	// Sending all 12 blocks is 2x overkill because the modulus in Transceiver::setModulus
	// for type IGPRS is set the same as type I which is only 26, not 52.
	RLCBSN_t bsn = FrameNumber2BSN(gBTS.time().FN()) + 1;
	for (int i = 0; i < 12; i++, bsn = bsn + 1) {
		GPRSLOG(1) <<"sendIdleFrame"<<LOGVAR2("TN",TN())<<LOGVAR(bsn)<<LOGVAR(i);
		mchDownlink->sendIdleFrame(bsn);
	}
	mchOldFec->setGPRS(true,this);
	debug_test();
}
void PDCHL1FEC::mchStop() {
	getRadio()->setSlot(TN(),Transceiver::I);
	mchOldFec->setGPRS(false,NULL);
}

PDCHL1FEC::PDCHL1FEC(TCHFACCHLogicalChannel *wlogchan) :
	PDCHCommon(this), mchOldFec(wlogchan->debugGetL1()), mchLogChan(wlogchan), mchTFIs(gTFIs)
{
	// Warning: These initializations use some of the variables in the init list above.
	mchUplink = new PDCHL1Uplink(this);
	mchDownlink = new PDCHL1Downlink(this);
	Stats.countPDCH++;
}

std::ostream& operator<<(std::ostream& os, PDCHL1FEC *ch)
{
	os << (ch ? ch->shortId() : "PDCH#(null)");
	return os;
}

#if 0
PDCHL1FEC::PDCHL1FEC()
	: PDCHCommon(this)
{
	mchUplink = new PDCHL1Uplink(this);
	mchDownlink = new PDCHL1Downlink(this);
	mchOldFec = NULL;
	mchLogChan = NULL;
	mchTFIs = gTFIs;
	//mchOpen();
}
// We dont really need to remember the LogicalChannel, but maybe we'll need it in the future.
void PDCHL1FEC::mchOpen(TCHFACCHLogicalChannel *wlogchan)
{
	// setGPRS has two affects: getTCH will consider the channel in-use;
	// and bursts start being delivered to us.
	// Note that the getTCH function normally doesnt even pay attention to whether
	// the channel is 'open' or not; it calls recyclable() that checks timers
	// and reuses the chan if they have expired.
	mchLogChan = wlogchan;
	devassert(mchLogChan->inUseByGPRS());
	mchOldFec = mchLogChan->debugGetL1();
	//mchReady = true;		// finally

}
#endif

PDCHL1FEC::~PDCHL1FEC() {
	gL2MAC.macForgetCh(this);
	delete mchUplink;
	delete mchDownlink;
}


void PDCHL1FEC::mchDump(std::ostream&os, bool verbose)
{
	os << " PDCH"<<LOGVAR2("ARFCN", ARFCN())
		<< LOGVAR2("TN",TN()) << LOGVAR2("FER",fmtfloat2(100.0*FER())) << "%" << "\n";
	if (verbose) {
		os <<"\t"; dumpReservations(os);
		os <<"\t"; usfDump(os);
		os <<"\t"; mchTFIs->tfiDump(os);
	}
}

//void PDCHL1Downlink::rollForward()
//{
//	// Calculate the TDMA paramters for the next transmission.
//	// This implements GSM 05.02 Clause 7 for the transmit side.
//	mchPrevWriteTime = mchNextWriteTime;
//	mchTotalBursts++;
//	mchNextWriteTime.rollForward(mchMapping.frameMapping(mchTotalBursts),mchMapping.repeatLength());
//}

#if FEC_DEBUG
GprsDecoder debugDecoder;
#endif

// Send the specified BitVector at the specified block time.
void PDCHL1Downlink::transmit(RLCBSN_t bsn, BitVector *mI, const int *qbits, int transceiverflags)
{
	parent()->debug_test();
	// Format the bits into the bursts.
	// GSM 05.03 4.1.5, 05.02 5.2.3
	// NO! Dont do a wait here.  The MAC serviceloop does this for all channels.
	// waitToSend();		// Don't get too far ahead of the clock.

	ARFCNManager *radio = getRadio();
	if (!radio) {
		// For some testing, we might not have a radio connected.
		// That's OK, as long as we know it.
		GLOG(INFO) << "XCCHL1Encoder with no radio, dumping frames";
		return;
	}

	int fn = bsn.FN();
	int tn = TN();

	for (int qi=0,B=0; B<4; B++) {
		Time nextWriteTime(fn,tn | transceiverflags);
		mchBurst.time(nextWriteTime);
		// Copy in the "encrypted" bits, GSM 05.03 4.1.5, 05.02 5.2.3.
		//OBJLOG(DEBUG) << "transmit mI["<<B<<"]=" << mI[B];
		mI[B].segment(0,57).copyToSegment(mchBurst,3);
		mI[B].segment(57,57).copyToSegment(mchBurst,88);
		mchBurst.Hl(qbits[qi++]);
		mchBurst.Hu(qbits[qi++]);
		// Send it to the radio.
		//OBJLOG(DEBUG) << "transmit mchBurst=" << mchBurst;
		if (gConfig.getBool("Control.GSMTAP.GPRS")) {
			// Send to GSMTAP.
			gWriteGSMTAP(ARFCN(),TN(),gBSNNext.FN(),
					TDMA_PDCH,
					false,	// not SACCH
					false,	// this is a downlink
					mchBurst,
					GSMTAP_TYPE_UM_BURST);
		}
#if FEC_DEBUG
		if (1) {
			// Try decoding the frame we just encoded to see if it is correct.
			devassert(mchBurst.size() == gSlotLen);
			RxBurst rxb(mchBurst);
			ChannelCodingType cc;
			decodeLowSide(rxb, B, debugDecoder, &cc);
		}
#endif
		radio->writeHighSideTx(mchBurst,"GPRS");
		fn++;	// This cannot overflow because it is within an RLC block.
	}
}

static GSM::TypeAndOffset frame2GsmTapType(BitVector &frame)
{
	switch (frame.peekField(0,2)) {	// Mac control field.
		case MACPayloadType::RLCControl:
			return TDMA_PACCH;
		case MACPayloadType::RLCData:
		default:
			return TDMA_PDCH;
	}
}

void PDCHL1Downlink::send1Frame(BitVector& frame,ChannelCodingType encoding, bool idle)
{
	if (!idle && gConfig.getBool("Control.GSMTAP.GPRS")) {
		// Send to GSMTAP.
		gWriteGSMTAP(ARFCN(),TN(),gBSNNext.FN(),
				frame2GsmTapType(frame),
				false,	// not SACCH
				false,	// this is a downlink
				frame);	// The data.
	}

	switch (encoding) {
	case ChannelCodingCS1:
		// Process the 184 bit (23 byte) frame, leave result in mI.
		//mchCS1Enc.encodeFrame41(frame,0);
		//transmit(gBSNNext,mchCS1Enc.mI,qCS1,0);
		mchEnc.encodeCS1(frame);
		transmit(gBSNNext,mchEnc.mI,qCS1,0);
		break;
	case ChannelCodingCS4:
		//std::cout << "WARNING: Using CS4\n";
		// This did not help the 3105/3101 errors:
		//mchCS4Enc.initCS4();	// DEBUG TEST!!  Didnt help.
		//mchCS4Enc.encodeCS4(frame);	// Result left in mI[].
		//transmit(gBSNNext,mchCS4Enc.mI,qCS4,0);
		mchEnc.encodeCS4(frame);	// Result left in mI[].
		transmit(gBSNNext,mchEnc.mI,qCS4,0);
		break;
	default:
		LOG(ERR) << "unrecognized GPRS channel coding " << (int)encoding;
		devassert(0);
	}
}


// Return true if we send a block on the downlink.
bool PDCHL1Downlink::send1DataFrame(
	RLCDownEngine *engdown,
	RLCDownlinkDataBlock *block,	// block to send.
	int makeres,					// 0 = no res, 1 = optional res, 2 = required res.
	MsgTransactionType mttype,	// Type of reservation
	unsigned *pcounter)
{
	//ScopedLock lock(testlock);
	TBF *tbf = engdown->getTBF();
	if (! setMACFields(block,mchParent,tbf,makeres,mttype,pcounter)) { return false; }
	// The rest of the RLC header is already set, but we did not know the tfi
	// when we created the RLCDownlinkDataBlocks (because tbf not yet attached)
	// so set tfi now that we know.  Update 8-2012: Above comment is stale because we
	// make the RLCDownlinkBlocks on the fly now.
	block->mTFI = tbf->mtTFI;
	// block->mPR = 1;	// DEBUG test; made no diff.

	tbf->talkedDown();

	BitVector tobits = block->getBitVector(); // tobits deallocated when this function exits.
	if (block->mChannelCoding == 0) { devassert(tobits.size() == 184); }
	if (GPRSDebug & 1) {
		RLCBlockReservation *res = mchParent->getReservation(gBSNNext);
		std::ostringstream sshdr;
		block->text(sshdr,false); //block->RLCDownlinkDataBlockHeader::text(sshdr);
		ByteVector content(tobits);
		GPRSLOG(1) << "send1DataFrame "<<parent()<<" "<<tbf<<LOGVAR(tbf->mtExpectedAckBSN)
			<< " "<<sshdr.str()
			<<" "<<(res ? res->str() : "")
			<< LOGVAR2("content",content);
		//<< " enc="<<tbf->mtChannelCoding <<" "<<os.str() << "\nbits:" <<bits.str();
		//<<" " <<block->str() <<"\nbits:" <<tobits.hexstr();
	}
#if FEC_DEBUG
	BitVector copybits; copybits.clone(tobits);
#endif
	send1Frame(tobits,block->mChannelCoding,0);
#if FEC_DEBUG
	BitVector *result = debugDecoder.getResult();
	devassert(result);
	devassert(copybits == tobits);
	if (result && !(*result == tobits)) {
		int diffbit = -1;
		char thing[500];
		for (int i = 0; i < (int)result->size(); i++) {
			thing[i] = '-';
			if (result->bit(i) != tobits.bit(i)) {
				if (diffbit == -1) diffbit = i;
				thing[i] = '0' + result->bit(i);
			}
		}
		thing[result->size()] = 0;
		GPRSLOG(1) <<"encoding error" <<LOGVAR2("cs",(int)debugDecoder.getCS())
			<<LOGVAR(diffbit)
			<<LOGVAR2("in:size",tobits.size()) <<LOGVAR2("out:size",result->size())
			<<"\n"<<tobits
			<<"\n"<<*result
			<<"\n"<<thing;
	} else {
		//GPRSLOG(1) <<"encoding ok" <<LOGVAR2("cs",(int)debugDecoder.getCS());
	}
#endif
	return true;
}

// Send the idle frame at specified Transceiver::SET_FILLER_FRAME)
// Do not call send1msgframe, because it would try to set MAC fields.
void PDCHL1Downlink::sendIdleFrame(RLCBSN_t bsn)
{
	RLCMsgPacketDownlinkDummyControlBlock *msg = new RLCMsgPacketDownlinkDummyControlBlock();
	devassert(msg->mUSF == 0);
	BitVector tobits(RLCBlockSizeInBits[ChannelCodingCS1]);
	msg->write(tobits);
	delete msg;
	//mchCS1Enc.encodeFrame41(tobits,0);
	//transmit(bsn,mchCS1Enc.mI,qCS1,Transceiver::SET_FILLER_FRAME);
	mchEnc.encodeCS1(tobits);
	transmit(bsn,mchEnc.mI,qCS1,Transceiver::SET_FILLER_FRAME);
}

void PDCHL1Downlink::bugFixIdleFrame()
{
	// DEBUG: We are only using this function to fix this problem for now.
	if (gFixIdleFrame) {
		// For this debug purpose, the mssage is sent on the next frame
		// TODO: debug purpose only! This only works for one channel!
		//Time tnext(gBSNNext.FN());
		//gBTS.clock().wait(tnext);
	}

	// Did we make it in time?
	{
	Time tnow = gBTS.time();
	int fn = tnow.FN();
	int mfn = (fn / 13);			// how many 13-multiframes
	int rem = (fn - (mfn*13));	// how many blocks within the last multiframe.
	int tbsn = mfn * 3 + ((rem==12) ? 2 : (rem/4));
	GPRSLOG(2) <<"idleframe"<<LOGVAR(fn)<<LOGVAR(tbsn)<<LOGVAR(rem);
	}

	/***
	if (mchIdleFrame.size() == 0) {
		RLCMsgPacketDownlinkDummyControlBlock *dummymsg = new RLCMsgPacketDownlinkDummyControlBlock();
		mchIdleFrame.set(BitVector(RLCBlockSizeInBits[ChannelCodingCS1]));
		dummymsg->write(mchIdleFrame);
		delete dummymsg;
	}
	send1Frame(mchIdleFrame,ChannelCodingCS1,true);
	***/
}

// Return true if we send a block on the downlink.
bool PDCHL1Downlink::send1MsgFrame(
	TBF *tbf,					// The TBF sending the message, or NULL for an idle frame.
	RLCDownlinkMessage *msg,	// The message.
	int makeres,			// 0 = no res, 1 = optional res, 2 = required res.
	MsgTransactionType mttype,	// Type of reservation
	unsigned *pcounter)		// If non-null, incremented if a reservation is made.
{
	if (! setMACFields(msg,mchParent,msg->mTBF,makeres,mttype,pcounter)) {
		delete msg;	// oh well.  
		return false;	// This allows some other tbf to try to use this downlink block.
	}
	
	bool dummy = msg->mMessageType == RLCDownlinkMessage::PacketDownlinkDummyControlBlock;
	bool idle = dummy && msg->isMacUnused();
	if (idle && 0 == gConfig.getNum("GPRS.SendIdleFrames")) {
		delete msg;		// Let the transceiver send an idle frame.
		return false;	// This return value will not be checked.
	}

	if (tbf) { tbf->talkedDown(); }

	// Convert to a BitVector.  Messages always use CS-1 encoding.
	BitVector tobits(RLCBlockSizeInBits[ChannelCodingCS1]);
	msg->write(tobits);
	// The possible downlink debug things we want to see are:
	// 2: Only non-dummy messages.
	// 32: include messages with non-idle MAC header, means mUSF or mSP.
	// 1024: all messages including dummy ones.
	if (GPRSDebug) {
		if ((!dummy && (GPRSDebug&2)) || (!idle && (GPRSDebug&32)) || (GPRSDebug&1024)) {
			ByteVector content(tobits);
			GPRSLOG(2|32|1024) << "send1MsgFrame "<<parent()
				<<" "<<msg->mTBF<< " "<<msg->str()
				<< " " <<LOGVAR2("content",content);
				// The res is unrelated to the message, and confusing, so dont print it:
				//<<" "<<(res ? res->str() : "");
		}
	}

#if 0
	// The below is what went out in release 3.0:
	if (GPRSDebug & (1|32)) {
		//RLCBlockReservation *res = mchParent->getReservation(gBSNNext);
		//std::ostringstream ssres;
		//if (res) ssres << res;
		if (! idle || (GPRSDebug & 1024)) {
			//ostringstream bits;
			//tobits.hex(bits);
			//GPRSLOG(1) << "send1MsgFrame "<<msg->mTBF<< " "<<msg->str() << "\nbits:"<<tobits.hexstr();
			ByteVector content(tobits);
			GPRSLOG(1) << "send1MsgFrame "<<parent()<<" "<<msg->mTBF<< " "<<msg->str() <<" "
				<< LOGVAR2("content",content);
			// This res is unrelated to the message, and confusing, so dont print it:
				//<<" "<<(res ? res->str() : "");
		} else if (msg->mUSF) {
			GPRSLOG(32) << "send1MsgFrame "<<parent()<<" "<<msg->mTBF<< " "<<msg->str() <<" ";
				//<<" "<<(res ? res->str() : "");
		}
	}
#endif

	delete msg;
	send1Frame(tobits,ChannelCodingCS1,idle);
	return true;
}


void PDCHL1Downlink::initBursts(L1FEC *oldfec)
{
	// unused: mchFillerBurst = GSM::TxBurst(GSM::gDummyBurst);	// TODO: This may not be correct.
											// Should probably be RLC Dummy control message.

	// Set up the training sequence since they'll be the same for all bursts.
	// training sequence, GSM 05.02 5.2.3
	// (pat) Set from the BSC color-code from the original GSM channel.
	GSM::gTrainingSequence[oldfec->TSC()].copyToSegment(mchBurst,61);
}


// Determine CS from the qbits.
ChannelCodingType GprsDecoder::getCS()
{
	// TODO: Make this more robust.
	// Currently we only support CS1 or CS4, so just look at the first bit.
	if (qbits[0]) {
		return ChannelCodingCS1;
	} else {
		return ChannelCodingCS4;
	}
}

BitVector *GprsDecoder::getResult()
{
	switch (getCS()) {
	case ChannelCodingCS4:
		return &mD_CS4;
	case ChannelCodingCS1:
		return &mD;
	default: devassert(0);	// Others not supported yet.
		return NULL;
	}
}

bool GprsDecoder::decodeCS4()
{
	// Incoming data is in SoftVector mC(456) and has already been deinterleaved.
	// Convert the SoftVector directly into bits: data + parity:
	// The first 12 bits need to be reconverted to 3 bits of usf.
	// Yes, they do this even on uplink, where there is no usf 5.03 sec 5.1.
	// Parity is run on the remaining 447 (=456-12+3) bits, which consists
	// of 424 of useful data, 7 bits of zeros, and 16 bits of parity.
	unsigned reverseUsf = decodeUSF(mC);
	mDP_CS4.fillField(0,reverseUsf,3);
	// We are grubbing into the arrays.  TODO: move this into the classes somewhere.
	float *in = mC.begin() + 12;
	char *out = mDP_CS4.begin() + 3;
	for (int i = 12; i < 456; i++) {
		*out++ = *in++ > 0.5 ? 1 : 0;
	}
	BitVector parity(mDP_CS4.segment(440-12+3,16));
	parity.invert();
	unsigned syndrome = mBlockCoder_CS4.syndrome(mDP_CS4);
	// Result is in mD_CS4.
	return (syndrome==0);
}

// Process the 184 bit frame, starting at offset, add parity, encode.
// Result is left in mI, representing 4 radio bursts.
void GprsEncoder::encodeCS1(const BitVector &src)
{
	encodeFrame41(src,0);
}

static BitVector mCcopy;
void GprsEncoder::encodeCS4(const BitVector &src)
{
	//if (sFecDebug) GPRSLOG(1) <<"encodeCS4 src\n"<<src;
	src.copyToSegment(mD_CS4,0,53*8);
	//if (sFecDebug) GPRSLOG(1) <<"encodeCS4 mD_CS4\n"<<mD_CS4;
	// mC.zero();	// DEBUG TEST!!  Did not help.
	mD_CS4.fillField(53*8,0,7);		// zero out 7 spare bits.
	mD_CS4.LSB8MSB();	// Ignores the last incomplete byte of 7 zero bits.
	//if (sFecDebug) GPRSLOG(1) <<"mC before parity\n"<<mC;
	// Parity is computed on original D before doing the USF translation above.
	mBlockCoder_CS4.writeParityWord(mD_CS4,mP_CS4);
	// Note that usf has been moved to the first three bits by the byte swapping above,
	// so when we write the 12 bits of GPRSUSFEncoding for usf into mC, it will overwrite
	// the original 3 parity bits.
	int reverseUsf = mD_CS4.peekField(0,3);
	// mU overwrites the first 3 bits of mD within mC.
	mU_CS4.fillField(0,GPRS::GPRSUSFEncoding[reverseUsf],12);
	// Result is left in mC.
	devassert(mC.peekField(0,12) == (unsigned) GPRS::GPRSUSFEncoding[reverseUsf]);
	devassert(mC.peekField(433,7) == 0);	// unused bits not modified.
	//if (sFecDebug) GPRSLOG(1) <<"mC before interleave\n"<<mC;

	interleave41();	// Interleaves mC into mI.
}


// Return decoded frame if success and B == 3, otherwise NULL.
static BitVector *decodeLowSide(const RxBurst &inBurst, int B, GprsDecoder &decoder, ChannelCodingType *ccPtr)
{
	inBurst.data1().copyToSegment(decoder.mI[B],0);
	inBurst.data2().copyToSegment(decoder.mI[B],57);
	// Save the stealing bits:
	// TODO: Save these as floats and do a correlation to pick the encoding.
	decoder.qbits[2*B] = inBurst.Hl();
	decoder.qbits[2*B+1] = inBurst.Hu();

	if (B != 3) { return NULL; }

	decoder.deinterleave();

	bool success;
	BitVector *result;
	switch ((*ccPtr = decoder.getCS())) {
	case ChannelCodingCS4:
		success = decoder.decodeCS4();
		LOG(DEBUG) << "CS-4 success=" << success;
		result = &decoder.mD_CS4;
		break;
	case ChannelCodingCS1:
		success = decoder.decode();
		LOG(DEBUG) << "CS-1 success=" << success;
		result = &decoder.mD;
		break;
	default: devassert(0);	// Others not supported yet.
		return NULL;
	}

	if (success) {
		result->LSB8MSB();
		return result;
	}
	return NULL;
}

#if 0	// Moved to SoftVector
// 1 is perfect and 0 means all the bits were 0.5
static float getEnergy(const SoftVector &vec,float &low)
{
	int len = vec.size();
	avg = 0; low = 1;
	for (int i = 0; i < len; i++) {
		float bit = vec[i];
		float energy = 2*((bit < 0.5) ? (0.5-bit) : (bit-0.5));
		if (energy < low) low = energy;
		avg += energy/len;
	}
}
#endif

// WARNING: This func runs in a separate thread.
void PDCHL1Uplink::writeLowSideRx(const RxBurst &inBurst)
{
	float low, avg = inBurst.getEnergy(&low);
	//if (avg > 0.7) { OBJLOG(DEBUG) << "PDCHL1Uplink " << inBurst; }

	//ScopedLock lock(testlock);
	int burstfn = inBurst.time().FN();
	int mfn = (burstfn / 13);			// how many 13-multiframes
	int rem = (burstfn - (mfn*13));	// how many blocks within the last multiframe.
	int B = rem % 4;

	if (avg > 0.5) { GPRSLOG(256) << "FEC:"<<LOGVAR(B)<<" "<<inBurst<<LOGVAR(avg); }

	ChannelCodingType cc;
	BitVector *result = decodeLowSide(inBurst,B,mchCS14Dec,&cc);

	if (B == 3) {
		int burst_fn=burstfn-3;	// First fn in rlc block.
		RLCBSN_t bsn = FrameNumber2BSN(burst_fn);

		if (GPRSDebug) {
			PDCHL1FEC *pdch = parent();
			short *qbits = mchCS14Dec.qbits;
			BitVector cshead(mchCS14Dec.mC.head(12).sliced());

			RLCBlockReservation *res = mchParent->getReservation(bsn);
			int thisUsf = pdch->getUsf(bsn-2);
			// If we miss a reservation or usf, print it:
			int missedRes = avg>0.4 && !result && (res||thisUsf);
			if (missedRes || (GPRSDebug & (result?4:256))) {
				std::ostringstream ss;
				char buf[30];
			 	ss <<"writeLowSideRx "<<parent()
					<<(result?" === good" : "=== bad")
					<< (res?" res:" : "") <<(res ? res->str() : "")
					//<<LOGVAR(cshead)
					//<<LOGVAR2("cs",(int)mchCS14Dec.getCS())
					<<LOGVAR(cc)
					<<LOGVAR2("revusf",decodeUSF(mchCS14Dec.mC))
					<<LOGVAR(burst_fn)<<LOGVAR(bsn) 
					<<LOGVAR2("RSSI",inBurst.RSSI()) <<LOGVAR2("TE",inBurst.timingError())
					// But lets print out the USFs bracketing this on either side.
					<<getAnsweringUsfText(buf,bsn)
					//<<" AnsweringUsf="<<pdch->getUsf(bsn-2)<<" "<<pdch->getUsf(bsn-1)
					//<<" ["<<pdch->getUsf(bsn)<<"] "<<pdch->getUsf(bsn+1)<<" "<<pdch->getUsf(bsn+2)
					<<" qbits="<<qbits[0]<<qbits[1]<<qbits[2]<<qbits[3]
							   <<qbits[4]<<qbits[5]<<qbits[6]<<qbits[7]
					<<LOGVAR(low)<<LOGVAR(avg)
					;
				if (missedRes) {
					for (int i = 0; i < 4; i++) {
						// There was an unanswered reservation or usf.
						avg = mchCS14Dec.mI[i].getEnergy(&low);
						GPRSLOG(1) << "energy["<<i<<"]:"<<LOGVAR(avg)<<LOGVAR(low)<<" "
							<<mchCS14Dec.mI[i];
					}
				}
				GLOG(DEBUG)<<ss.str();
				// Make sure we see a decoder failure if it reoccurs.
				if (missedRes) std::cout <<ss.str() <<"\n";
			}
		} // if GPRSDebug

		if (result) {
			// Check clock skew for debugging purposes.
			static int cnt = 0;
			if (bsn >= gBSNNext-1) {
				if (cnt++ % 32 == 0) {
					GLOG(ERR) << "Incoming burst at frame:"<<burst_fn
						<<" is not sufficiently ahead of clock:"<<gBSNNext.FN();
					if (GPRSDebug) {
					std::cout << "Incoming burst at frame:"<<burst_fn
						<<" is not sufficiently ahead of clock:"<<gBSNNext.FN()<<"\n";
					}
				}
			}

			countGoodFrame();

			// The four frame radio block has been decoded and is in mD.
			if (gConfig.getBool("Control.GSMTAP.GPRS")) {
				// Send to GSMTAP.  Untested.
				gWriteGSMTAP(ARFCN(),TN(),gBSNNext.FN(), //GSM::TDMA_PACCH,
						frame2GsmTapType(*result),
						false,	// not SACCH
						true,	// this is an uplink.
						*result);	// The data.
			}

			mchUplinkData.write(new RLCRawBlock(bsn,*result,inBurst.RSSI(),inBurst.timingError(),cc));
		} else {
			countBadFrame();
		}
	} else {
		// We dont have a full 4 bursts yet, and we rarely care about these
		// intermediate results, but here is a way to see them:
		GPRSLOG(64) <<"writeLowSideRx "<<parent()<<LOGVAR(burstfn)<<LOGVAR(B) 
			<<" RSSI=" <<inBurst.RSSI() << " timing=" << inBurst.timingError();
	}
}

// This is an entry point from other directories.
// Just bind the inburst with the associated channel and call the routine above.
void GPRSWriteLowSideRx(const GSM::RxBurst& inBurst, PDCHL1FEC*pdch)
{
	pdch->uplink()->writeLowSideRx(inBurst);
}


// Transfer one RLCBlock, which is four frames, to the radio layer.
// Copied from XXCHL1Encoder::transmit()
// void PDCHL1Downlink::transmit(mI)
// See XCCHL1Encoder:transmit(mI)

// Code duplicated almost verbatim from L1Encoder.
// Inability to share code embedded in a method is one of the problems
// with object oriented programming.
#if 0
void PDCHL1Downlink::mchResync()
{
	// If the encoder's clock is far from the current BTS clock,
	// get it caught up to something reasonable.
	Time now = gBTS.time();
	int32_t delta = mchNextWriteTime-now;
	GPRSLOG(8) << "PDCHL1Downlink" <<LOGVAR(mchNextWriteTime)
			<<LOGVAR(now)<< LOGVAR(delta);
	if ((delta<0) || (delta>(51*26))) {
		mchNextWriteTime = now;
		//mchNextWriteTime.TN(now.TN());	// unneeded?
		mchNextWriteTime.rollForward(mchMapping.frameMapping(mchTotalBursts),mchMapping.repeatLength());
		GPRSLOG(2) <<"PDCHL1Downlink RESYNC" << LOGVAR(mchNextWriteTime) << LOGVAR(now);
	}
}
#endif


// Dispatch an RLC block on this downlink.
// This must run once for every Radio Block (4 TDMA frames or so) sent.
// It should be kept only as far enough ahead of the physical layer so that it never stalls.
// Based on: TCHFACCHL1Encoder::dispatch()
void PDCHL1Downlink::dlService()
{
	// Get right with the system clock.
	// NO: mchResync();
	static int debugCntTotal = 0, debugCntDummy = 0;
	debugCntTotal++;
	if ((GPRSDebug&512) || debugCntTotal % 1024 == 0) {
		GPRSLOG(2) << "dlService sent total="<<debugCntTotal<<" dummy="<<debugCntDummy <<this->parent();
	}

	// If gFixIdleFrame only send blocks on the even BSNs,
	// and send idle frames on the odd BSNs, to make SURE that the
	// RRBP and USF fields are cleared.
	// This means that only the odd uplink BSNs will be used for uplink data.
	// I also modified makeReservationInt to make the RRBP uplink reservations
	// only on even frames, since they will be clear of data, but
	// that is overkill for debugging.
	// For reservations all we care is that the downlink RRBP field
	// occurs only on even frames -
	// the associated uplink block will be 3-7 blocks downtime from that,
	// so could land on either even or odd frames.
	if (gFixIdleFrame && (gBSNNext & 1)) { return; }

	// I think we have to check active because the radio can get turned on/off
	// completely without our knowledge.
	// If this happens, the TBFs will expire, so we should probably destroy
	// the entire GPRS machinery and start over when we get turned back on.
	//if (! active()) {
	//	mNextWriteTime += 52;	// Wait for a PCH multiframe.
	//	gBTS.clock().wait(mNextWriteTime);
	//	return;
	//}


	// Look for a data block to send.
	// We did not queue these up in advance because the data that the engine
	// wants to send may change every time it receives an ack/nack message.
	//TBFList_t &list = gL2MAC.macTBFs;
	//TBFList_t::iterator itr = list.begin(), e = list.end();
	//for ( ; itr != e; itr++) {
		//TBF *tbf = *itr;
	TBF *tbf;
	TBFList_t::iterator itr;
	for (RListIterator<TBF*> itrl(gL2MAC.macTBFs); itrl.next(tbf,itr); ) {
		if (!tbf->canUseDownlink(this)) {
			GPRSLOG(4) <<"dlService"<<tbf<<" state "<<tbf->mtGetState()
				<<" reqch:"<<tbf->mtMS->msPacch
				<< " can not use downlink:"<<this->parent();
			continue;
		}
		TBFState::type oldstate = tbf->mtGetState();

		if (tbf->mtServiceDownlink(this)) {
			GPRSLOG(2) <<"dlService"<<tbf<<LOGVAR(oldstate)<<" state="<<tbf->mtGetState()
				<<" reqch:"<<tbf->mtMS->msPacch
				<<" using ch:"<<this->parent();
			// Move this tbf to end of the list so we may service someone else next time.
			// TODO: If the tbf is using extended dynamic uplink, all ganged
			// uplink channels are reserved at once, and so we are not sharing
			// the other ganged uplinks with other TBFs that want to use them unless
			// those TBFs also share this channel.
			gL2MAC.macTBFs.erase(itr);
			gL2MAC.macTBFs.push_back(tbf);
			if (gFixIdleFrame) { bugFixIdleFrame(); }
			return;
		}
	}

	// If nothing else, send a dummy message.
	// We have to allocate it because we allocate all messages.
	// Note that this message will have the MAC header fields USF and RRBP set by send1MsgFrame.
	RLCMsgPacketDownlinkDummyControlBlock *dummymsg = new RLCMsgPacketDownlinkDummyControlBlock();
	send1MsgFrame(NULL,dummymsg,0,MsgTransNone,NULL);
	debugCntDummy++;

	if (gFixIdleFrame) { bugFixIdleFrame(); }
}


// This is just a pass-through to TFIList.
void PDCHCommon::setTFITBF(int tfi, RLCDir::type dir, TBF *tbf)
{
	mchParent->mchTFIs->setTFITBF(tfi,dir,tbf);
}

TBF *PDCHCommon::getTFITBF(int tfi, RLCDirType dir)
{
	TBF *tbf = mchParent->mchTFIs->getTFITBF(dir,tfi);
	if (tbf == NULL) {
		// Somehow we lost track of this tbf.  Maybe the timers are set wrong,
		// and we dumped it before the MS gave up on it.
		GLOG(ERR) << "GPRS Radio Block Data Block with unrecognized tfi: " << tfi << " dir:"<<dir;
		return NULL;
	}
	return tbf;
}

// Return the RF Power Control alpha and gamma value.
// ---------- From GSM05.08 10.2.1 ----------
// MS RF output power Pch is:
// Pch = min(Gamma0 - GammaCh - Alpha * (C + 48), PMAX)
// Alpha is a system param or optionally sent to MS in RLC messages (we dont.)
// C represents CCCH power measured inside the MS.
// Gamma0 = 39 dBm for GSM400, GSM900, GSM850, or 36 dBm for DCS1800 and PCS1900
// PMAX is MS_TXPWR_MAX_CCH if no PBCCH exists (our case.)
// ------------------------------------------
// The alpha value represents how much the MS should turn down its output
// power in response to its own measurement of CCCH input power.
// This alpha value is sent in the Packet Uplink/Downlink Assignment messages.
// The gamma value is subtracted from the max power.
// Basically, the BTS can take complete control of power by setting alpha to 0
// and using gamma, or let the MS take complete control by setting gamma to 0 and alpha to 1,
// or anything in between.
// TODO: Once we start talking to the MS, it will be RACHing to us on a regular basis,
// and will also be reporting lost bursts, so we could be fiddling with the power parameters 
// on a per-MS basis.  However, this function returns the default gamma alpha
// to be used for the initial single-block downlink assignment when we dont
// know what MS we are talking to yet.
// BEGINCONFIG
// 'GPRS.MS.Power.Alpha',10,1,0,'MS power control parameter; see GSM 05.08 10.2.1'
// 'GPRS.MS.Power.Gamma',31,1,0,'MS power control parameter; see GSM 05.08 10.2.1'
// ENDCONFIG
int GetPowerAlpha()
{
	// God only knows what this should be.
	int alpha = gConfig.getNum("GPRS.MS.Power.Alpha");
	// The value runs 0..10 representing increments of 10%
	return RN_BOUND(alpha,0,10);		// Bound to allowed values.
}

int GetPowerGamma()
{
	// 0 means full power and let the MS control power.
	int gamma = gConfig.getNum("GPRS.MS.Power.Gamma");
	return RN_BOUND(gamma,0,31);		// Bound to allowed values, 5 bits.
}

int GetTimingAdvance(float timingError)
{
	int initialTA = (int)(timingError + 0.5F);
	initialTA = RN_BOUND(initialTA,0,62);	// why 62, not 63?
	return initialTA;
}

#if 0
	// Turn on this PDCHL1
	// Unused function: This is how you would reprogram TRXManager.
	void PDCHL1FEC::snarf()
	{
		assert(! encoder()->active());
		assert(! decoder()->active());
		// TODO: Wait until an appropriate time, lock everything in sight before doing this.
		ARFCNManager*radio = encoder()->getRadio();
		// Connect to the radio now.
		mPDTCH.downstream(radio);
		mPTCCH.downstream(radio);
		mPDIdle.downstream(radio);
	}

	void PDCHL1FEC::unsnarf()
	{
		assert(mlogchan);
		ARFCNManager*radio = encoder()->getRadio();
		// TODO: Wait until an appropriate time, lock everything in sight before doing this.
		mlogchan->downstream(radio);
		// TODO: This is not thread safe.  It would not work either,
		// because the TRXManager function asserts that nobody got the channel earlier.
		// Can we hook the channel inside the logical channel?
		gBTS.addTCH(mlogchan);
	}
#endif
};
