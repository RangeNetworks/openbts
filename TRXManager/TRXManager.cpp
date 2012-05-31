/*
* Copyright 2008, 2010 Free Software Foundation, Inc.
*
* This software is distributed under the terms of the GNU Affero Public License.
* See the COPYING file in the main directory for details.
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




#include "TRXManager.h"
#include "GSMCommon.h"
#include "GSMTransfer.h"
#include "GSMLogicalChannel.h"
#include "GSMConfig.h"
#include "GSML1FEC.h"

#include <Logger.h>

#include <string>
#include <string.h>
#include <stdlib.h>

#undef WARNING


using namespace GSM;
using namespace std;


TransceiverManager::TransceiverManager(int numARFCNs,
		const char* wTRXAddress, int wBasePort)
	:mHaveClock(false),
	mClockSocket(wBasePort+100)
{
	// set up the ARFCN managers
	for (int i=0; i<numARFCNs; i++) {
		int thisBasePort = wBasePort + 1 + 2*i;
		mARFCNs.push_back(new ::ARFCNManager(wTRXAddress,thisBasePort,*this));
	}
}



void TransceiverManager::start()
{
	mClockThread.start((void*(*)(void*))ClockLoopAdapter,this);
	for (unsigned i=0; i<mARFCNs.size(); i++) {
		mARFCNs[i]->start();
	}
}





void* ClockLoopAdapter(TransceiverManager *transceiver)
{
	Timeval nextContact;
	while (1) {
		transceiver->clockHandler();
	}
	return NULL;
}



void TransceiverManager::clockHandler()
{
	char buffer[MAX_UDP_LENGTH];
	int msgLen = mClockSocket.read(buffer,3000);

	// Did the transceiver die??
	if (msgLen<0) {
		LOG(ALERT) << "TRX clock interface timed out, assuming TRX is dead.";
		abort();
	}

	if (msgLen==0) {
		LOG(ALERT) << "read error on TRX clock interface, return " << msgLen;
		return;
	}

	if (strncmp(buffer,"IND CLOCK",9)==0) {
		uint32_t FN;
		sscanf(buffer,"IND CLOCK %u", &FN);
		LOG(DEBUG) << "CLOCK indication, clock="<<FN;
		gBTS.clock().set(FN);
		mHaveClock = true;
		return;
	}

	buffer[msgLen]='\0';
	LOG(ALERT) << "bogus message " << buffer << " on clock interface";
}










::ARFCNManager::ARFCNManager(const char* wTRXAddress, int wBasePort, TransceiverManager &wTransceiver)
	:mTransceiver(wTransceiver),
	mDataSocket(wBasePort+100+1,wTRXAddress,wBasePort+1),
	mControlSocket(wBasePort+100,wTRXAddress,wBasePort)
{
	// The default demux table is full of NULL pointers.
	for (int i=0; i<8; i++) {
		for (unsigned j=0; j<maxModulus; j++) {
			mDemuxTable[i][j] = NULL;
		}
	}
}




void ::ARFCNManager::start()
{
	mRxThread.start((void*(*)(void*))ReceiveLoopAdapter,this);
}


void ::ARFCNManager::installDecoder(GSM::L1Decoder *wL1d)
{
	unsigned TN = wL1d->TN();
	const TDMAMapping& mapping = wL1d->mapping();

	// Is this mapping a valid uplink on this slot?
	assert(mapping.uplink());
	assert(mapping.allowedSlot(TN));

	LOG(DEBUG) << "ARFCNManager::installDecoder TN: " << TN << " repeatLength: " << mapping.repeatLength();

	mTableLock.lock();
	for (unsigned i=0; i<mapping.numFrames(); i++) {
		unsigned FN = mapping.frameMapping(i);
		while (FN<maxModulus) {
			// Don't overwrite existing entries.
			assert(mDemuxTable[TN][FN]==NULL);
			mDemuxTable[TN][FN] = wL1d;
			FN += mapping.repeatLength();
		}
	}
	mTableLock.unlock();
}




void ::ARFCNManager::writeHighSide(const GSM::TxBurst& burst)
{
	LOG(DEBUG) << "transmit at time " << gBTS.clock().get() << ": " << burst;
	// format the transmission request message
	static const int bufferSize = gSlotLen+1+4+1;
	char buffer[bufferSize];
	unsigned char *wp = (unsigned char*)buffer;
	// slot
	*wp++ = burst.time().TN();
	// frame number
	uint32_t FN = burst.time().FN();
	*wp++ = (FN>>24) & 0x0ff;
	*wp++ = (FN>>16) & 0x0ff;
	*wp++ = (FN>>8) & 0x0ff;
	*wp++ = (FN) & 0x0ff;
	// power level
	/// FIXME -- We hard-code gain to 0 dB for now.
	*wp++ = 0;
	// copy data
	const char *dp = burst.begin();
	for (unsigned i=0; i<gSlotLen; i++) {
		*wp++ = (unsigned char)((*dp++) & 0x01);
	}
	// write to the socket
	mDataSocketLock.lock();
	mDataSocket.write(buffer,bufferSize);
	mDataSocketLock.unlock();
}




void ::ARFCNManager::driveRx()
{
	// read the message
	char buffer[MAX_UDP_LENGTH];
	int msgLen = mDataSocket.read(buffer);
	if (msgLen<=0) SOCKET_ERROR;
	// decode
	unsigned char *rp = (unsigned char*)buffer;
	// timeslot number
	unsigned TN = *rp++;
	// frame number
	int32_t FN = *rp++;
	FN = (FN<<8) + (*rp++);
	FN = (FN<<8) + (*rp++);
	FN = (FN<<8) + (*rp++);
	// physcial header data
	signed char* srp = (signed char*)rp++;
	// reported RSSI is negated dB wrt full scale
	int RSSI = *srp;
	srp = (signed char*)rp++;
	// timing error comes in 1/256 symbol steps
	// because that fits nicely in 2 bytes
	int timingError = *srp;
	timingError = (timingError<<8) | (*rp++);
	// soft symbols
	float data[gSlotLen];
	for (unsigned i=0; i<gSlotLen; i++) data[i] = (*rp++) / 256.0F;
	// demux
	receiveBurst(RxBurst(data,GSM::Time(FN,TN),timingError/256.0F,-RSSI));
}


void* ReceiveLoopAdapter(::ARFCNManager* manager){
	while (true) {
		manager->driveRx();
		pthread_testcancel();
	}
	return NULL;
}





int ::ARFCNManager::sendCommandPacket(const char* command, char* response)
{
	int msgLen = 0;
	response[0] = '\0';

	LOG(INFO) << "command " << command;
	mControlLock.lock();

	for (int retry=0; retry<10; retry++) {
		mControlSocket.write(command);
		msgLen = mControlSocket.read(response,3000);
		if (msgLen>0) {
			response[msgLen] = '\0';
			break;
		}
		LOG(WARNING) << "TRX link timeout on attempt " << retry+1;
	}

	mControlLock.unlock();
	LOG(DEBUG) << "response " << response;

	if ((msgLen>4) && (strncmp(response,"RSP ",4)==0)) {
		return msgLen;
	}

	LOG(ALERT) << "lost control link to transceiver";
	SOCKET_ERROR;
}




int ::ARFCNManager::sendCommand(const char*command, int param, int *responseParam)
{
	// Send command and get response.
	char cmdBuf[MAX_UDP_LENGTH];
	char response[MAX_UDP_LENGTH];
	sprintf(cmdBuf,"CMD %s %d", command, param);
	int rspLen = sendCommandPacket(cmdBuf,response);
	if (rspLen<=0) return -1;
	// Parse and check status.
	char cmdNameTest[10];
	int status;
	cmdNameTest[0]='\0';
        if (!responseParam)
	  sscanf(response,"RSP %10s %d", cmdNameTest, &status);
        else
          sscanf(response,"RSP %10s %d %d", cmdNameTest, &status, responseParam);
	if (strcmp(cmdNameTest,command)!=0) return -1;
	return status;
}


int ::ARFCNManager::sendCommand(const char*command, const char* param)
{
	// Send command and get response.
	char cmdBuf[MAX_UDP_LENGTH];
	char response[MAX_UDP_LENGTH];
	sprintf(cmdBuf,"CMD %s %s", command, param);
	int rspLen = sendCommandPacket(cmdBuf,response);
	if (rspLen<=0) return -1;
	// Parse and check status.
	char cmdNameTest[10];
	int status;
	cmdNameTest[0]='\0';
	sscanf(response,"RSP %10s %d", cmdNameTest, &status);
	if (strcmp(cmdNameTest,command)!=0) return -1;
	return status;
}



int ::ARFCNManager::sendCommand(const char*command)
{
	// Send command and get response.
	char cmdBuf[MAX_UDP_LENGTH];
	char response[MAX_UDP_LENGTH];
	sprintf(cmdBuf,"CMD %s", command);
	int rspLen = sendCommandPacket(cmdBuf,response);
	if (rspLen<=0) return -1;
	// Parse and check status.
	char cmdNameTest[10];
	int status;
	cmdNameTest[0]='\0';
	sscanf(response,"RSP %10s %d", cmdNameTest, &status);
	if (strcmp(cmdNameTest,command)!=0) return -1;
	return status;
}




bool ::ARFCNManager::tune(int wARFCN)
{
	// convert ARFCN number to a frequency
	unsigned rxFreq = uplinkFreqKHz(gBTS.band(),wARFCN);
	unsigned txFreq = downlinkFreqKHz(gBTS.band(),wARFCN);
	// tune rx
	int status = sendCommand("RXTUNE",rxFreq);
	if (status!=0) {
		LOG(ALERT) << "RXTUNE failed with status " << status;
		return false;
	}
	// tune tx
	status = sendCommand("TXTUNE",txFreq);
	if (status!=0) {
		LOG(ALERT) << "TXTUNE failed with status " << status;
		return false;
	}
	// done
	mARFCN=wARFCN;
	return true;
}



bool ::ARFCNManager::tuneLoopback(int wARFCN)
{
	// convert ARFCN number to a frequency
	unsigned txFreq = downlinkFreqKHz(gBTS.band(),wARFCN);
	// tune rx
	int status = sendCommand("RXTUNE",txFreq);
	if (status!=0) {
		LOG(ALERT) << "RXTUNE failed with status " << status;
		return false;
	}
	// tune tx
	status = sendCommand("TXTUNE",txFreq);
	if (status!=0) {
		LOG(ALERT) << "TXTUNE failed with status " << status;
		return false;
	}
	// done
	mARFCN=wARFCN;
	return true;
}


bool ::ARFCNManager::powerOff()
{
	int status = sendCommand("POWEROFF");
	if (status!=0) {
		LOG(ALERT) << "POWEROFF failed with status " << status;
		return false;
	}
	return true;
}


bool ::ARFCNManager::powerOn()
{
	int status = sendCommand("POWERON");
	if (status!=0) {
		LOG(ALERT) << "POWERON failed with status " << status;
		return false;
	}
	return true;
}





bool ::ARFCNManager::setPower(int dB)
{
	int status = sendCommand("SETPOWER",dB);
	if (status!=0) {
		LOG(ALERT) << "SETPOWER failed with status " << status;
		return false;
	}
	return true;
}


bool ::ARFCNManager::setTSC(unsigned TSC) 
{
	assert(TSC<8);
	int status = sendCommand("SETTSC",TSC);
	if (status!=0) {
		LOG(ALERT) << "SETTSC failed with status " << status;
		return false;
	}
	return true;
}


bool ::ARFCNManager::setSlot(unsigned TN, unsigned combination)
{
	assert(TN<8);
	assert(combination<8);
	char paramBuf[MAX_UDP_LENGTH];
	sprintf(paramBuf,"%d %d", TN, combination);
	int status = sendCommand("SETSLOT",paramBuf);
	if (status!=0) {
		LOG(ALERT) << "SETSLOT failed with status " << status;
		return false;
	}
	return true;
}

bool ::ARFCNManager::setMaxDelay(unsigned km)
{
        int status = sendCommand("SETMAXDLY",km);
        if (status!=0) {
                LOG(ALERT) << "SETMAXDLY failed with status " << status;
                return false;
        }
        return true;
}

signed ::ARFCNManager::setRxGain(signed rxGain)
{
        signed newRxGain;
        int status = sendCommand("SETRXGAIN",rxGain,&newRxGain);
        if (status!=0) {
                LOG(ALERT) << "SETRXGAIN failed with status " << status;
                return false;
        }
        return newRxGain;
}

signed ::ARFCNManager::getNoiseLevel(void)
{
	signed noiselevel;
	int status = sendCommand("NOISELEV",0,&noiselevel);
        if (status!=0) {
                LOG(ALERT) << "NOISELEV failed with status " << status;
                return false;
        }
        return noiselevel;
}

void ::ARFCNManager::receiveBurst(const RxBurst& inBurst)
{
	LOG(DEBUG) << "receiveBurst: " << inBurst;
	uint32_t FN = inBurst.time().FN() % maxModulus;
	unsigned TN = inBurst.time().TN();

	mTableLock.lock();
	L1Decoder *proc = mDemuxTable[TN][FN];
	if (proc==NULL) {
		LOG(DEBUG) << "ARFNManager::receiveBurst in unconfigured TDMA position TN: " << TN << " FN: " << FN << ".";
		mTableLock.unlock();
		return;
	}
	proc->writeLowSide(inBurst);
	mTableLock.unlock();
}


// vim: ts=4 sw=4
