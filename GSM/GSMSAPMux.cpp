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
	OBJLOG(DEBUG) << frame;
	unsigned SAPI;

	// (pat) Add switch to validate upstream primitives.  The upstream only generates a few primitives;
	// the rest are created in L2LAPDm.
	switch (frame.primitive()) {
		case HANDOVER_ACCESS:	// Only send this on SAPI 0.
			SAPI = 0;
			break;
		case DATA:
			SAPI = frame.SAPI();	
			break;
		case ESTABLISH:		// (pat) Do we really want to send this on all SAPI?
		case ERROR:
			// If this is a non-data primitive, copy it out to every SAP.
			for (int i=0; i<4; i++) {
				if (mUpstream[i]) mUpstream[i]->writeLowSide(frame);
			}
			return;
		default:
		case RELEASE:		// Sent downstream, but not upstream.
		case UNIT_DATA:		// Only used above L2.
		case HARDRELEASE:	// Sent downstream, but not upstream.
			// If you get this assertion, make SURE you know what will happen upstream to that primitive.
			assert(0);
			return;		// make g++ happy.
	}
	if (mUpstream[SAPI]) {
		mUpstream[SAPI]->writeLowSide(frame);
	} else {
		LOG(WARNING) << "received DATA for unsupported SAP " << SAPI;
	}
	return;
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
