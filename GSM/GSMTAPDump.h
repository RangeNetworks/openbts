/*
* Copyright 2008, 2009 Free Software Foundation, Inc.
* This software is distributed under multiple licenses; see the COPYING file in the main directory for licensing information for this specific distribuion.
*
    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

*
* This use of this software may be subject to additional restrictions.
* See the LEGAL file in the main directory for details.

*/



#ifndef GSMTAPDUMP_H
#define GSMTAPDUMP_H

#include "gsmtap.h"
#include "GSMTransfer.h"


void gWriteGSMTAP(unsigned ARFCN, unsigned TS, unsigned FN, const GSM::L2Frame& frame);


#endif

// vim: ts=4 sw=4
