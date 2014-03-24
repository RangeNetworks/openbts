/**@file RRLPServer */
/*
* Copyright 2008, 2009, 2010 Free Software Foundation, Inc.
* Copyright 2010 Kestrel Signal Processing, Inc.
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

#ifndef RRLPSERVER_H
#define RRLPSERVER_H
#include <GSML3CommonElements.h>	// For L3MobileIdentity
#include "ControlCommon.h"

namespace Control {

class RRLPServer
{
	public:
		RRLPServer(string wSubscriber);
		bool trouble() { return mTrouble; }
		void rrlpSend(L3LogicalChannel *mmchan);
		void rrlpRecv(const GSM::L3Message*);
	private:
		RRLPServer(); // not allowed
		void serve();
		string mURL;
		// GSM::L3MobileIdentity mMobileID;
		//L3LogicalChannel *mDCCH;		(pat) The channel is passed as an arg every time this is called, and it could change, so dont cache it.
		string mQuery;
		string mName;
		bool mTrouble;
		map<string,string> mResponse;
		// Before assist can be restored, mAPDUs is going to have to go into more persistent storage.
		// TransactionEntry for call?  Dunno for sms or lur.
		vector<string> mAPDUs;
};

bool sendRRLP(GSM::L3MobileIdentity wMobileID, L3LogicalChannel *wLCH);
void recvRRLP(MMContext *wLCH, const GSM::L3Message *wMsg);

};

#endif
