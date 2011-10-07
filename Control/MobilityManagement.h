/**@file GSM/SIP Mobility Management, GSM 04.08. */
/*
* Copyright 2008, 2009, 2010 Free Software Foundation, Inc.
* Copyright 2010 Kestrel Signal Processing, Inc.
*
* This software is distributed under multiple licenses; see the COPYING file in the main directory for licensing information for this specific distribuion.
*
* This use of this software may be subject to additional restrictions.
* See the LEGAL file in the main directory for details.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

*/

#ifndef MOBILITYMANAGEMENT_H
#define MOBILITYMANAGEMENT_H


namespace GSM {
class LogicalChannel;
class L3CMServiceRequest;
class L3LocationUpdatingRequest;
class L3IMSIDetachIndication;
};

namespace Control {

void CMServiceResponder(const GSM::L3CMServiceRequest* cmsrq, GSM::LogicalChannel* DCCH);

void IMSIDetachController(const GSM::L3IMSIDetachIndication* idi, GSM::LogicalChannel* DCCH);

void LocationUpdatingController(const GSM::L3LocationUpdatingRequest* lur, GSM::LogicalChannel* DCCH);

}


#endif
