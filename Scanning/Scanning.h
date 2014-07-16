/**@file Classes for managing BCCH channel scanning and related database tables. */
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

#ifndef SCANNING_H
#define SCANNING_H


#include <list>
#include <GSMCommon.h>

struct sqlite3;

namespace GSM {
	class L3SystemInformationType1;
	class L3SystemInformationType2;
	class L3SystemInformationType3;
}


typedef std::list<unsigned> ARFCNList;


/** A C++ API to the SPECTRUM_MAP database table. */
class SpectrumMap {

	private:

	sqlite3 *mDB;		///< database connection


	public:

	class LinkDirection {
		public:
			static const int Up;
			static const int Down;

			LinkDirection() {
				mDir = Up;
			}
			LinkDirection(const LinkDirection &that) {
				mDir = that.mDir;
			}
			LinkDirection(const int that) {
				mDir = that;
			}

			bool operator==(LinkDirection& that) const {
				return mDir == that.mDir;
			}
			bool operator==(int that) const {
				return mDir == that;
			}
			void operator=(int dir) {
				if (dir == Up || dir == Down) {
					mDir = dir;
				}
			}
			const char *string() const {
				if (mDir == Up) {
					return "up";
				} else if (mDir == Down) {
					return "down";
				}
				return "";
			}

		private:
			int mDir;
	};

	/** The constructor connects to the database and inits the table. */
	SpectrumMap(const char* path);

	/** The destructor closes the database connection. */
	~SpectrumMap();

	/** Clear the map. */
	void clear();

	/** Mark a given ARFCN as having a given power level. */
	void power(GSM::GSMBand band, unsigned ARFCN, float freq, LinkDirection& linkDir, float dBm);

	/** Mark a given ARFCN as having a given power level. */
	void power(GSM::GSMBand band, unsigned ARFCN, LinkDirection& linkDir, float dBm);

	/** Mark a given ARFCN as having a given power level. */
	//void power(GSM::GSMBand band, unsigned ARFCN, float freq, float dBm);

	/** Return a list of up to count of the most powerful ARFCNs in a given band. */
	ARFCNList topPower(GSM::GSMBand band, unsigned count) const;

	/** Return a list of up to count of the least powerful ARFCNs in a given band. */
	ARFCNList minimumPower(GSM::GSMBand band, unsigned count) const;

};


#if 0
class BTSRecord {

	private:

	GSM::GSMBand mBand;
	unsigned mC0;
	unsigned mMCC;
	unsigned mMNC;
	unsigned mLAC;
	unsigned mCI;
	unsigned mBSIC;
	float mFreqOffset;
	ARFCNList mNeighbors;
	ARFCNList mCA;

	public:

	BTSRecord(
		GSM::GSMBand wBand,
		unsigned wC0,
		unsigned wMCC, unsigned wMNC, unsigned wLAC,
		unsigned wCI,
		unsigned wBSIC,
		float wFreqOffset)
		:mBand(wBand),mC0(wC0),
		mMCC(wMCC),mMNC(wMNC),mLAC(wLAC),
		mCI(wCI),
		mBSIC(wBSIC),
		mFreqOffset(wFreqOffset)
	{ }

	void neighbors(const ARFCNList& wNeighbors)
		{ mNeighbors = wNeighbors; }

	void CA(const ARFCNList& wCA)
		{ mCA = wCA; }

	/** Return a key string based on MCC:MNC:LAC:CI that is unique for this BTS. */
	std::string keyString() const;

};
#endif


/** A list of ARFCNs waiting to be scanned. */
class ScanList {

	private:

	ARFCNList mNewARFCNs;		///< ARFCNs that still need to be scanned.
	ARFCNList mOldARFCNs;		///< ARFCNs that have already been scanned.

	public:

	size_t size() const { return mNewARFCNs.size(); }

	/** Add ARFCNs to the mNewARFCNs if it is not already scanned or pending for scanning. */
	void add(const ARFCNList& moreARFCNs);

	void add(const std::vector<unsigned>& moreARFCNs);

	/** Add an ARFCN to the mNewARFCNs if it is not already scanned or pending for scanning. */
	void add(unsigned ARFCN);

	int front() const { return mNewARFCNs.front(); }

	/** Pop and return the next ARFCN or -1 if the list is empty. */
	int next();

	/** Return true if the scan is complete. */
	bool allDone() const { return mNewARFCNs.size()==0; }

	/** Clear both lists; reset the object. */
	void clear();

	private:

	/** Return true if a given ARFCN is already in mNewARFCNs or mOldARFCNs. */
	bool alreadyListed(unsigned ARFCN) const;
};





#endif

// vim: ts=4 sw=4
