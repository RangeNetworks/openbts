/*
* Copyright 2011, 2014 Range Networks, Inc.
*
* This software is distributed under multiple licenses;
* see the COPYING file in the main directory for licensing
* information for this specific distribution.
*
* This use of this software may be subject to additional restrictions.
* See the LEGAL file in the main directory for details.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
*/

#define LOG_GROUP LogGroup::GSM		// Can set Log.Level.GSM for debugging

#include <iostream>

#include <GSML3RRMessages.h>
#include <GSMTransfer.h>

// Load configuration from a file.
ConfigurationTable gConfig("OpenBTS.config");

int main()
{
    GSM::L3ApplicationInformation ai();
    static const char init_request_msbased_gps[4] = {'@', '\x01', 'x', '\xa8'}; // pre encoded PER for the following XER:
    static std::vector<char> request_msbased_gps(init_request_msbased_gps,
        init_request_msbased_gps + sizeof(init_request_msbased_gps));
    GSM::L3ApplicationInformation ai2(request_msbased_gps);

    GSM::L3Frame f(ai2);
    std::cout << f;

    return 0;
}

