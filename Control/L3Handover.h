/*
* Copyright 2014 Range Networks, Inc.
*

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

* This software is distributed under multiple licenses;
* see the COPYING file in the main directory for licensing
* information for this specific distribution.
*
* This use of this software may be subject to additional restrictions.
* See the LEGAL file in the main directory for details.
*/

#ifndef L3HANDOVER_H
#define L3HANDOVER_H

namespace GSM {
	class L3MeasurementResults;
	class SACCHLogicalChannel;
};

namespace Control {
	class L3LogicalChannel;
	class TranEntry;

void ProcessHandoverAccess(L3LogicalChannel *chan);
bool outboundHandoverTransfer(TranEntry* transaction, L3LogicalChannel *TCH);
void HandoverDetermination(const GSM::L3MeasurementResults* measurements, GSM::SACCHLogicalChannel* sacch);
};
#endif
