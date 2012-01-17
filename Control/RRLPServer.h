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

namespace GSM {
class LogicalChannel;
class L3MobileIdentity;
};

class RRLPServer
{
	public:
		RRLPServer(GSM::L3MobileIdentity wMobileID, GSM::LogicalChannel *wDCCH);
		// tell server to send location assistance to mobile
		bool assist();
		// tell server to ask mobile for location
		bool locate();
	private:
		RRLPServer(); // not allowed
		string url;
		GSM::L3MobileIdentity mobileID;
		GSM::LogicalChannel *DCCH;
		string query;
		string name;
		bool transact();
		bool trouble;
};

bool sendRRLP(GSM::L3MobileIdentity mobileID, GSM::LogicalChannel *LCH);

#endif
