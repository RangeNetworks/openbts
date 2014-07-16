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

#include <ostream>
#include <Globals.h>
#include <Configuration.h>
#include "CLI.h"

namespace CommandLine {


/** Print or modify the global configuration table. */
CLIStatus configCmd(string mode, int argc, char** argv, ostream& os)
{
	// no args, just print
	if (argc==1) {
		ConfigurationKeyMap::iterator mp = gConfig.mSchema.begin();
		while (mp != gConfig.mSchema.end()) {
			if (mode.compare("customer") == 0) {
				if (mp->second.getVisibility() == ConfigurationKey::CUSTOMER ||
					mp->second.getVisibility() == ConfigurationKey::CUSTOMERSITE ||
					mp->second.getVisibility() == ConfigurationKey::CUSTOMERTUNE ||
					mp->second.getVisibility() == ConfigurationKey::CUSTOMERWARN) {
						ConfigurationKey::printKey(mp->second, gConfig.getStr(mp->first), os);
				}
			} else if (mode.compare("developer") == 0) {
				ConfigurationKey::printKey(mp->second, gConfig.getStr(mp->first), os);
			}
			mp++;
		}
		return SUCCESS;
	}

	// one arg
	if (argc==2) {
		// matches exactly? print single key
		if (gConfig.keyDefinedInSchema(argv[1])) {
			ConfigurationKey::printKey(gConfig.mSchema[argv[1]], gConfig.getStr(argv[1]), os);
			ConfigurationKey::printDescription(gConfig.mSchema[argv[1]], os);
			os << endl;
		// ...otherwise print all similar keys
		} else {
			int foundCount = 0;
			ConfigurationKeyMap matches = gConfig.getSimilarKeys(argv[1]);
			ConfigurationKeyMap::iterator mp = matches.begin();
			while (mp != matches.end()) {
				if (mode.compare("customer") == 0) {
					if (mp->second.getVisibility() == ConfigurationKey::CUSTOMER ||
						mp->second.getVisibility() == ConfigurationKey::CUSTOMERSITE ||
						mp->second.getVisibility() == ConfigurationKey::CUSTOMERTUNE ||
						mp->second.getVisibility() == ConfigurationKey::CUSTOMERWARN) {
							ConfigurationKey::printKey(mp->second, gConfig.getStr(mp->first), os);
							foundCount++;
					}
				} else if (mode.compare("developer") == 0) {
					ConfigurationKey::printKey(mp->second, gConfig.getStr(mp->first), os);
					foundCount++;
				}
				mp++;
			}
			if (!foundCount) {
				os << argv[1] << " - no keys matched";
				if (mode.compare("customer") == 0) {
					os << ", developer/factory keys can be accessed with \"devconfig\".";
				} else if (mode.compare("developer") == 0) {
					os << ", custom keys can be accessed with \"rawconfig\".";
				}
				os << endl;
			}
		}
		return SUCCESS;
	}

	// >1 args: set new value
	string val;
	for (int i=2; i<argc; i++) {
		val.append(argv[i]);
		if (i!=(argc-1)) val.append(" ");
	}
	if (!gConfig.keyDefinedInSchema(argv[1])) {
		os << argv[1] << " is not a valid key, change failed. If you're trying to define a custom key/value pair (e.g. the Log.Level.Filename.cpp pairs), use \"rawconfig\"." << endl;
		return SUCCESS;
	}
	if (mode.compare("customer") == 0) {
		if (gConfig.mSchema[argv[1]].getVisibility() == ConfigurationKey::DEVELOPER) {
			os << argv[1] << " should only be changed by developers. Use \"devconfig\" if you are ABSOLUTELY sure this needs to be changed." << endl;
			return SUCCESS;
		}
		if (gConfig.mSchema[argv[1]].getVisibility() == ConfigurationKey::FACTORY) {
			os << argv[1] << " should only be set once by the factory. Use \"devconfig\" if you are ABSOLUTELY sure this needs to be changed." << endl;
			return SUCCESS;
		}
	}
	if (!gConfig.isValidValue(argv[1], val)) {
		os << argv[1] << " new value \"" << val << "\" is invalid, change failed.";
		if (mode.compare("developer") == 0) {
			os << " To override the configuration value checks, use \"rawconfig\".";
		}
		os << endl;
		return SUCCESS;
	}

	string previousVal = gConfig.getStr(argv[1]);
	if (val.compare(previousVal) == 0) {
		os << argv[1] << " is already set to \"" << val << "\", nothing changed" << endl;
		return SUCCESS;
	}
// TODO : removing of default values from DB disabled for now. Breaks webui.
//	if (val.compare(gConfig.mSchema[argv[1]].getDefaultValue()) == 0) {
//		if (!gConfig.remove(argv[1])) {
//			os << argv[1] << " storing new value (default) failed" << endl;
//			return SUCCESS;
//		}
//	} else {
		if (!gConfig.set(argv[1],val)) {
			os << "DB ERROR: " << argv[1] << " could not be updated" << endl;
			return FAILURE;
		}
//	}
	os << argv[1] << " changed from \"" << previousVal << "\" to \"" << val << "\"" << endl;

	return SUCCESS;
}

/** Disable a configuration key. */
CLIStatus unconfig(int argc, char** argv, ostream& os)
{
	if (argc!=2) return BAD_NUM_ARGS;

	if (!gConfig.defines(argv[1])) {
		os << argv[1] << " is not in the table" << endl;
		return BAD_VALUE;
	}

	if (gConfig.keyDefinedInSchema(argv[1]) && !gConfig.isValidValue(argv[1], "")) {
		os << argv[1] << " is not disableable" << endl;
		return BAD_VALUE;
	}

	if (!gConfig.set(argv[1], "")) {
		os << "DB ERROR: " << argv[1] << " could not be disabled" << endl;
		return FAILURE;
	}

	os << argv[1] << " disabled" << endl;

	return SUCCESS;
}


/** Set a configuration value back to default or remove from table if custom key. */
CLIStatus rmconfig(int argc, char** argv, ostream& os)
{
	if (argc!=2) return BAD_NUM_ARGS;

	if (!gConfig.defines(argv[1])) {
		os << argv[1] << " is not in the table" << endl;
		return BAD_VALUE;
	}

	// TODO : removing of default values from DB disabled for now. Breaks webui.
	if (gConfig.keyDefinedInSchema(argv[1])) {
		if (!gConfig.set(argv[1],gConfig.mSchema[argv[1]].getDefaultValue())) {
			os << "DB ERROR: " << argv[1] << " could not be set back to the default value" << endl;
			return FAILURE;
		}

		os << argv[1] << " set back to its default value" << endl;
		vector<string> warnings = gConfig.crossCheck(argv[1]);
		vector<string>::iterator warning = warnings.begin();
		while (warning != warnings.end()) {
			os << "WARNING: " << *warning << endl;
			warning++;
		}
		if (gConfig.isStatic(argv[1])) {
			os << argv[1] << " is static; change takes effect on restart" << endl;
		}
		return SUCCESS;
	}

	if (!gConfig.remove(argv[1])) {
		os << "DB ERROR: " << argv[1] << " could not be removed from the configuration table" << endl;
		return FAILURE;
	}

	os << argv[1] << " removed from the configuration table" << endl;

	return SUCCESS;
}


/** Print or modify the global configuration table. */
CLIStatus rawconfig(int argc, char** argv, ostream& os)
{
	// no args, just print
	if (argc==1) {
		gConfig.find("",os);
		return SUCCESS;
	}

	// one arg, pattern match and print
	if (argc==2) {
		gConfig.find(argv[1],os);
		return SUCCESS;
	}

	// >1 args: set new value
	string val;
	for (int i=2; i<argc; i++) {
		val.append(argv[i]);
		if (i!=(argc-1)) val.append(" ");
	}
	bool existing = gConfig.defines(argv[1]);
	string previousVal;
	if (existing) {
		previousVal = gConfig.getStr(argv[1]);
	}
	if (!gConfig.set(argv[1],val)) {
		os << "DB ERROR: " << argv[1] << " change failed" << endl;
		return FAILURE;
	}
	if (gConfig.isStatic(argv[1])) {
		os << argv[1] << " is static; change takes effect on restart" << endl;
	}
	if (!existing) {
		os << "defined new config " << argv[1] << " as \"" << val << "\"" << endl;
	} else {
		os << argv[1] << " changed from \"" << previousVal << "\" to \"" << val << "\"" << endl;
	}
	return SUCCESS;
}

};	// namespace
