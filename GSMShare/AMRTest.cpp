/*
* Copyright 2012, 2014 Range Networks, Inc.
*
* This software is distributed under multiple licenses; see the COPYING file in the main directory for licensing information for this specific distribution.
*
* This use of this software may be subject to additional restrictions.
* See the LEGAL file in the main directory for details.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

*/

#include "BitVector.h"
#include "AmrCoder.h"
#include <iostream>
#include <cstdlib>
#include <string.h>
#include "GSM503Tables.cpp"

using namespace std;
// We must have a gConfig now to include BitVector.
#include "Configuration.h"
ConfigurationTable gConfig;

BitVector randomBitVector(int n)
{
	BitVector t(n);
	for (int i = 0; i < n; i++) t[i] = random()%2;
	return t;
}

#if OLD_TEST
// Doug wrote this AMR test code
void testEncodeDecode(const char *label, void (*encoder)(BitVector&,BitVector&), void (*decoder)(SoftVector&,BitVector&), unsigned frameSize, unsigned iRate, unsigned order, bool debug)
#else
// Pat modified to use the Viterbi base class.
void testEncodeDecode(const char *label, ViterbiBase *coder, unsigned frameSize, unsigned iRate, unsigned order, bool debug)
#endif
{
	const unsigned trials = 10;
	cout << endl;
	if (debug) cout << label << endl;
	BitVector v1 = randomBitVector(frameSize);
	if (debug) cout << "v1 " << v1 << endl;
	BitVector v2(iRate*frameSize+iRate*order);
#if OLD_TEST
	(*encoder)(v1, v2);
#else
	coder->encode(v1,v2);
#endif
	if (debug) cout << "v2 " << v2 << endl;
	for (int perr = 0; perr <= 50; perr += 10) {
		int errs = 0;
		// multiple trials smooths out the weirdnesses due to unfortunate positioning of noisy bits
		for (unsigned trial = 0; trial < trials; trial++) {
			SoftVector sv2(v2);
			for (unsigned j = 0; j < sv2.size(); j++) {
				if (random() % 100 < perr) sv2[j] = 0.5;
			}
			if (debug) cout << "n2 " << sv2 << endl;
			BitVector v3(frameSize);
#if OLD_TEST
			decoder(sv2, v3);
#else
			coder->decode(sv2,v3);
#endif
			if (debug) cout << "v3 " << v3 << endl;
			for (unsigned j = 0; j < frameSize; j++) {
				if (v1.bit(j) != v3.bit(j)) errs++;
			}
		}
		int pbbo = 100 * errs / (frameSize * trials);
		cout << label << ", " << "% ambiguous bits in : % bad bits out = " << perr << " : " << pbbo << endl;
	}
}

#if OLD_TEST
//void encoder12_2(BitVector &v1, BitVector &v2)   { ViterbiTCH_AFS12_2 vCoder; v1.encode(vCoder,v2); } 
//void decoder12_2(SoftVector &sv2, BitVector &v3) { ViterbiTCH_AFS12_2 vCoder; sv2.decode(vCoder,v3); }
//
//void encoder10_2(BitVector &v1, BitVector &v2)   { ViterbiTCH_AFS10_2 vCoder; v1.encode(vCoder,v2); } 
//void decoder10_2(SoftVector &sv2, BitVector &v3) { ViterbiTCH_AFS10_2 vCoder; sv2.decode(vCoder,v3); }
//
//void encoder7_95(BitVector &v1, BitVector &v2)   { ViterbiTCH_AFS7_95 vCoder; v1.encode(vCoder,v2); } 
//void decoder7_95(SoftVector &sv2, BitVector &v3) { ViterbiTCH_AFS7_95 vCoder; sv2.decode(vCoder,v3); }
//
//void encoder7_4(BitVector &v1, BitVector &v2)   { ViterbiTCH_AFS7_4 vCoder; v1.encode(vCoder,v2); } 
//void decoder7_4(SoftVector &sv2, BitVector &v3) { ViterbiTCH_AFS7_4 vCoder; sv2.decode(vCoder,v3); }
//
//void encoder6_7(BitVector &v1, BitVector &v2)   { ViterbiTCH_AFS6_7 vCoder; v1.encode(vCoder,v2); } 
//void decoder6_7(SoftVector &sv2, BitVector &v3) { ViterbiTCH_AFS6_7 vCoder; sv2.decode(vCoder,v3); }
//
//void encoder5_9(BitVector &v1, BitVector &v2)   { ViterbiTCH_AFS5_9 vCoder; v1.encode(vCoder,v2); } 
//void decoder5_9(SoftVector &sv2, BitVector &v3) { ViterbiTCH_AFS5_9 vCoder; sv2.decode(vCoder,v3); }
//
//void encoder5_15(BitVector &v1, BitVector &v2)   { ViterbiTCH_AFS5_15 vCoder; v1.encode(vCoder,v2); } 
//void decoder5_15(SoftVector &sv2, BitVector &v3) { ViterbiTCH_AFS5_15 vCoder; sv2.decode(vCoder,v3); }
//
//void encoder4_75(BitVector &v1, BitVector &v2)   { ViterbiTCH_AFS4_75 vCoder; v1.encode(vCoder,v2); } 
//void decoder4_75(SoftVector &sv2, BitVector &v3) { ViterbiTCH_AFS4_75 vCoder; sv2.decode(vCoder,v3); }
#endif

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


void testAMR()
{
#if OLD_TEST
	testEncodeDecode("12_2", encoder12_2, decoder12_2, 250, 2, 4, false);
	testEncodeDecode("10_2", encoder10_2, decoder10_2, 210, 3, 4, false);
	testEncodeDecode("7_95", encoder7_95, decoder7_95, 165, 3, 6, false);
	testEncodeDecode("7_4",  encoder7_4,  decoder7_4,  154, 3, 4, false);
	testEncodeDecode("6_7",  encoder6_7,  decoder6_7,  140, 4, 4, false);
	testEncodeDecode("5_9",  encoder5_9,  decoder5_9,  124, 4, 6, false);
	testEncodeDecode("5_15", encoder5_15, decoder5_15, 109, 5, 4, false);
	testEncodeDecode("4_75", encoder4_75, decoder4_75, 101, 5, 6, false);
#else
	testEncodeDecode("12_2", new ViterbiTCH_AFS12_2(), 250, 2, 4, false);
	testEncodeDecode("10_2", new ViterbiTCH_AFS10_2(), 210, 3, 4, false);
	testEncodeDecode("7_95", new ViterbiTCH_AFS7_95(), 165, 3, 6, false);
	testEncodeDecode("7_4",  new ViterbiTCH_AFS7_4(),  154, 3, 4, false);
	testEncodeDecode("6_7",  new ViterbiTCH_AFS6_7(),  140, 4, 4, false);
	testEncodeDecode("5_9",  new ViterbiTCH_AFS5_9(),  124, 4, 6, false);
	testEncodeDecode("5_15", new ViterbiTCH_AFS5_15(), 109, 5, 4, false);
	testEncodeDecode("4_75", new ViterbiTCH_AFS4_75(), 101, 5, 6, false);
#endif

	testPunctureUnpuncture("12.2", GSM::gAMRPuncturedTCH_AFS12_2,  60);
	testPunctureUnpuncture("10.2", GSM::gAMRPuncturedTCH_AFS10_2, 194);
	testPunctureUnpuncture("7.95", GSM::gAMRPuncturedTCH_AFS7_95,  65);
	testPunctureUnpuncture("7.4",  GSM::gAMRPuncturedTCH_AFS7_4,   26);
	testPunctureUnpuncture("6.7",  GSM::gAMRPuncturedTCH_AFS6_7,  128);
	testPunctureUnpuncture("5.9",  GSM::gAMRPuncturedTCH_AFS5_9,   72);
	testPunctureUnpuncture("5.15", GSM::gAMRPuncturedTCH_AFS5_15, 117);
	testPunctureUnpuncture("4.75", GSM::gAMRPuncturedTCH_AFS4_75,  87);
}

int main(int argc, char *argv[])
{
	testAMR();
}
