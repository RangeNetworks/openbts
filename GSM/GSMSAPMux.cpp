/*
* Copyright 2008, 2009 Free Software Foundation, Inc.
*
* This software is distributed under multiple licenses; see the COPYING file in the main directory for licensing information for this specific distribuion.
*
* This use of this software may be subject to additional restrictions.
* See the LEGAL file in the main directory for details.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

*/



#include "GSMSAPMux.h"
#include "GSMTransfer.h"
#include "GSML1FEC.h"
#include "GSML2LAPDm.h"

#include <Logger.h>


using namespace GSM;


void SAPMux::writeHighSide(const L2Frame& frame)
{
	// The SAP may or may not be present, depending on the channel type.
	OBJLOG(DEBUG) << frame;
	ScopedLock lock(mLock);
	mDownstream->writeHighSide(frame);
}



void SAPMux::writeLowSide(const L2Frame& frame)
{
	OBJLOG(DEBUG) << frame.SAPI() << " " << frame;
	unsigned SAPI = frame.SAPI();	
	bool data = frame.primitive()==DATA;
	if (data && (!mUpstream[SAPI])) {
		LOG(WARNING) << "received DATA for unsupported SAP " << SAPI;
		return;
	}
	if (data) {
		mUpstream[SAPI]->writeLowSide(frame);
	} else {
		// If this is a non-data primitive, copy it out to every SAP.
		for (int i=0; i<4; i++) {
			if (mUpstream[i]) mUpstream[i]->writeLowSide(frame);
		}
	}
}



void LoopbackSAPMux::writeHighSide(const L2Frame& frame)
{
	OBJLOG(DEBUG) << "Loopback " << frame;
	// Substitute primitive
	L2Frame newFrame(frame);
	unsigned SAPI = frame.SAPI();	
	switch (frame.primitive()) {
		case ERROR: SAPI=0; break;
		case RELEASE: return;
		default: break;
	}
	// Because this is a test fixture, as assert here.
	// If this were not a text fixture, we would print a warning
	// and ignore the frame.
	assert(mUpstream[SAPI]);
	ScopedLock lock(mLock);
	mUpstream[SAPI]->writeLowSide(newFrame);
}



void LoopbackSAPMux::writeLowSide(const L2Frame& frame)
{
	assert(mDownstream);
	L2Frame newFrame(frame);
	mDownstream->writeHighSide(newFrame);
}





// vim: ts=4 sw=4
