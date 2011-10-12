/**@file GSM/SIP Mobility Management, GSM 04.08. */
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
