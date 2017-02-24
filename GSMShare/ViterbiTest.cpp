/*
* Copyright 2008, 2014 Free Software Foundation, Inc.
* Copyright 2014 Range Networks, Inc.
*
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




#include "BitVector.h"
#include "ViterbiR204.h"
#include <iostream>
#include <cstdlib>
#include <string.h>
#include <sys/time.h>
#include "GSM503Tables.cpp"
#ifndef LOGVAR
#define LOGVAR(var) (" " #var "=") << var
#define LOGVAR2(name,val) " " << name << "=" << (val)
#endif
 
using namespace std;

// We must have a gConfig now to include BitVector.
#include "Configuration.h"
ConfigurationTable gConfig;


void origTest()
{
	BitVector v0("0000111100111100101011110000");
	cout << v0 << endl;
	// (pat) The conversion from a string was inserting garbage into the result BitVector.
	// Fixed now so only 0 or 1 are inserted, but lets check:
	for (char *cp = v0.begin(); cp < v0.end(); cp++) cout << (int)*cp<<" ";
	cout << endl;

	BitVector v1(v0);
	v1.LSB8MSB();
	cout <<v1 << " (byte swapped)" << endl;

	// Test operator==
	assert(v1 == v1);
	assert(!(v0 == v1));

	ViterbiR2O4 vCoder;
	BitVector v2(v1.size()*2);
	//v1.encode(vCoder,v2);
	vCoder.encode(v1,v2);		// v1 encoded into v2
	cout <<v2 << endl;
	SoftVector sv2(v2);
	cout << sv2 << endl;
	BitVector v3(v1.size());
	//sv2.decode(vCoder,v3);
	vCoder.decode(sv2,v3);		// v2 decoded into v3.
	cout << v3 << " (encoded/decoded)" << endl;
	assert(v1.size() == v3.size());
	assert(0 == memcmp(v1.begin(),v3.begin(),v1.size()));
	assert(v3 == v1);	// This tests operator==

	cout << v3.segment(3,4) << endl;

	BitVector v4(v3.segment(0,4),v3.segment(8,4));
	cout << v4 << endl;

	BitVector v5("000011110000");
	int r1 = v5.peekField(0,8);
	int r2 = v5.peekField(4,4);
	int r3 = v5.peekField(4,8);
	cout << r1 <<  ' ' << r2 << ' ' << r3 << endl;
	cout << v5 << endl;
	v5.fillField(0,0xa,4);
	int r4 = v5.peekField(0,8);
	cout << v5 << endl;
	cout << r4 << endl;

	v5.reverse8();
	cout << v5 << endl;

	BitVector mC = "000000000000111100000000000001110000011100001101000011000000000000000111000011110000100100001010000010100000101000001010000010100000010000000000000000000000000000000000000000000000001100001111000000000000000000000000000000000000000000000000000010010000101000001010000010100000101000001010000001000000000000000000000000110000111100000000000001110000101000001100000001000000000000";
	SoftVector mCS(mC);
	BitVector mU(mC.size()/2);
	//mCS.decode(vCoder,mU);
	vCoder.decode(mCS,mU);
	// (pat) And just what should the result be?
	cout << "c=" << mCS << endl;
	cout << "u=" << mU << endl;


	unsigned char ts[9] = "abcdefgh";
	BitVector tp(70);
	cout << "ts=" << ts << endl;
	tp.unpack(ts);
	cout << "tp=" << tp << endl;
	tp.pack(ts);
	cout << "ts=" << ts << endl;

	BitVector v6("010101");
	BitVector v7(3);
	unsigned punk[3] = {1,2,5};
	v6.copyPunctured(v7, punk, 3);
	cout << "v7=" << v7 << endl;
}

BitVector randomBitVector(int n)
{
	BitVector t(n);
	for (int i = 0; i < n; i++) t[i] = random()%2;
	return t;
}

// Doug wrote this AMR test code
// Pat modified to use the Viterbi base class.
void testEncodeDecode(const char *label, ViterbiBase *coder, unsigned frameSize, unsigned iRate, unsigned order, bool debug)
{
	const bool isAMR = false;
	const unsigned trials = 10;
	cout << endl;
	if (debug) cout << label << endl;
	BitVector v1 = randomBitVector(frameSize);
	if (debug) cout << "v1 " << v1 << endl;
	int resultsize;
	if (isAMR) {
		resultsize = iRate*frameSize+iRate*order;
	} else {
		resultsize = iRate*frameSize;
	}
	BitVector v2(resultsize);


	cout << LOGVAR(v1.size()) <<LOGVAR(v2.size()) << endl;
	coder->encode(v1,v2);
	if (debug) cout << "v2 " << v2 << endl;
	int bec;
	unsigned bitErrors;

	for (float badness = 0.0; badness <= 1.0; badness += 0.1)
	  for (int perr = 0; perr <= 50; perr += 10) {
		int errsOut = 0;
		int errsIn = 0;
		// multiple trials smooths out the weirdnesses due to unfortunate positioning of noisy bits
		for (unsigned trial = 0; trial < trials; trial++) {
			SoftVector sv2(v2);
			for (unsigned j = 0; j < sv2.size(); j++) {
				if (random() % 100 < perr) {
					// (pat) old test, this was not very good because 0.5 always maps to 1.
					// sv2[j] = 0.5;
					//sv2[j] = 1.0 - sv2[j];
					sv2[j] = sv2.bit(j) ? 1.0 - badness : badness;
					errsIn++;
				}
			}
			if (debug) cout << "n2 " << sv2 << endl;
			BitVector v3(frameSize);
			coder->decode(sv2,v3);
			bec = coder->getBEC();
			if (debug) cout << "v3 " << v3 << endl;
			for (unsigned j = 0; j < frameSize; j++) {
				if (v1.bit(j) != v3.bit(j)) errsOut++;
			}

			// Lets try re-encoding it and see what happens.
			BitVector try2(resultsize);
			coder->encode(v3,try2);
			// The try should be equivalent to sv2?
			bitErrors = 0;
			for (unsigned k = 0; k < try2.size(); k++) {
				if (try2[k] != sv2.bit(k)) bitErrors++;
			}
		}
		int pbbo = 100 * errsOut / (frameSize * trials);
		//cout << label << ", " << "% ambiguous bits in : % bad bits out = " << perr << " : " << pbbo <<LOGVAR(bec)<<LOGVAR(bitErrors)<<endl;
		cout << label <<LOGVAR(badness)<<LOGVAR2("bad bits in",errsIn) <<" "<<perr<<"%"
			<<LOGVAR2("bad_bits_out",errsOut) <<" "<<pbbo<<"%"   <<LOGVAR(bec)<<LOGVAR(bitErrors)<<endl;
	}
}


void testPunctureUnpuncture(const char *label, const unsigned int *punk, size_t plth)
{
	bool ok = true;
	// Things could go wrong and be missed due to we're just wrangling bit vectors,
	// so run each speed a few times to reduce the probability of missing an error.
	for (int i = 0; i < 5; i++) {
		// generate orig
		BitVector orig = randomBitVector(448+plth);
		// puncture
		BitVector punctured(orig.size() - plth);
		orig.copyPunctured(punctured, punk, plth);
		SoftVector softPunctured(punctured);
		// unpuncture
		SoftVector unpunctured(orig.size());
		softPunctured.copyUnPunctured(unpunctured, punk, plth);
		// verify
		const unsigned *p = punk;
		for (unsigned i = 0; i < orig.size(); i++) {
			if (unpunctured[i] == 0.5) {
				if (*p++ != i) ok = false;
			} else {
				if (orig.bit(i) != unpunctured[i]) ok = false;
			}
		}
		ok = ok && (p == punk + plth);
	}
	cout << "puncture->unpuncture " << label << " " << (ok ? "ok" : "NOT ok") << endl;
}

int main(int argc, char *argv[])
{
	struct timeval tv;
	gettimeofday(&tv,NULL);
	srandom(tv.tv_usec);
	origTest();
	testEncodeDecode("ViterbiR204", new ViterbiR2O4(), 378, 2, 4, false);
}
