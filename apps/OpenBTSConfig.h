/*
* Copyright 2014 Range Networks, Inc.
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

#ifndef _CONFIGKEYS_H_
#define _CONFIGKEYS_H_ 1

#include <Configuration.h>

// pat 3-2014: Added a better way to get these config values.
class OpenBTSConfig : public ConfigurationTable {
	public:
	OpenBTSConfig(const char* filename, const char *wCmdName, ConfigurationKeyMap wSchema) :
		ConfigurationTable(filename, wCmdName, wSchema)
		{}
    	OpenBTSConfig(void) {}; // used by CLI

	void configUpdateKeys();

	// This structure mirrors the config variable names in GetConfigurationKeys.cpp.
	// Any value added here must also be added to configUpdateKeys(), which function is
	// called after any change to the config to update the values in this structure.
	struct GSM {
		struct Handover {
			int FailureHoldoff;
			int Margin;
			int Ny1;

			struct History { int Max; } History;
			struct Noise { int Factor; } Noise;

			struct RXLEV_DL { float Target; int History, Margin, PenaltyTime; } RXLEV_DL;
		} Handover;
		struct {
			struct Power { int Min, Max, Damping; } Power;
			struct TA { int Damping, Max; } TA;
		} MS;
		struct {
			int T3103, T3105, T3109, T3113, T3212;
		} Timer;
		struct {
			int RADIO_LINK_TIMEOUT;
		} BTS;
	} GSM;
};

extern OpenBTSConfig gConfig;
#endif
