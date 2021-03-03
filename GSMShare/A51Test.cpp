/*
 * A pedagogical implementation of A5/1.
 *
 * Copyright (C) 1998-1999: Marc Briceno, Ian Goldberg, and David Wagner
 *
 * The source code below is optimized for instructional value and clarity.
 * Performance will be terrible, but that's not the point.
 * The algorithm is written in the C programming language to avoid ambiguities
 * inherent to the English language. Complain to the 9th Circuit of Appeals
 * if you have a problem with that.
 *
 * This software may be export-controlled by US law.
 *
 * This software is free for commercial and non-commercial use as long as
 * the following conditions are adhered to.
 * Copyright remains the authors' and as such any Copyright notices in
 * the code are not to be removed.
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 * 
 * The license and distribution terms for any publicly available version or
 * derivative of this code cannot be changed.  i.e. this code cannot simply be
 * copied and put under another distribution license
 * [including the GNU Public License.]
 *
 * Background: The Global System for Mobile communications is the most widely
 * deployed cellular telephony system in the world. GSM makes use of
 * four core cryptographic algorithms, neither of which has been published by
 * the GSM MOU. This failure to subject the algorithms to public review is all  
 * the more puzzling given that over 100 million GSM
 * subscribers are expected to rely on the claimed security of the system.
 *
 * The four core GSM algorithms are:
 * A3		authentication algorithm
 * A5/1		"strong" over-the-air voice-privacy algorithm
 * A5/2		"weak" over-the-air voice-privacy algorithm
 * A8		voice-privacy key generation algorithm
 *
 * In April of 1998, our group showed that COMP128, the algorithm used by the
 * overwhelming majority of GSM providers for both A3 and A8
 * functionality was fatally flawed and allowed for cloning of GSM mobile
 * phones.
 * Furthermore, we demonstrated that all A8 implementations we could locate,
 * including the few that did not use COMP128 for key generation, had been
 * deliberately weakened by reducing the keyspace from 64 bits to 54 bits.
 * The remaining 10 bits are simply set to zero!
 *
 * See <A HREF="http://www.scard.org/gsm">http://www.scard.org/gsm</A> for additional information.
 *
 * The question so far unanswered is if A5/1, the "stronger" of the two
 * widely deployed voice-privacy algorithm is at least as strong as the
 * key. Meaning: "Does A5/1 have a work factor of at least 54 bits"?
 * Absent a publicly available A5/1 reference implementation, this question
 * could not be answered. We hope that our reference implementation below,
 * which has been verified against official A5/1 test vectors, will provide
 * the cryptographic community with the base on which to construct the
 * answer to this important question.
 *
 * Initial indications about the strength of A5/1 are not encouraging.
 * A variant of A5, while not A5/1 itself, has been estimated to have a
 * work factor of well below 54 bits. See http://jya.com/crack-a5.htm for
 * background information and references.
 * 
 * With COMP128 broken and A5/1 published below, we will now turn our attention
 * to A5/2. The latter has been acknowledged by the GSM community to have
 * been specifically designed by intelligence agencies for lack of security.
 *
 */


#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "./A51.h"
// We must have a gConfig now to include BitVector.
#include "Configuration.h"
ConfigurationTable gConfig;

/* Test the code by comparing it against
 * a known-good test vector. */
void test() {
	byte key[8] = {0xEF, 0xCD, 0xAB, 0x89, 0x67, 0x45, 0x23, 0x12};
	word frame = 0x134;
	byte goodAtoB[15] = { 0x53, 0x4E, 0xAA, 0x58, 0x2F, 0xE8, 0x15,
	                      0x1A, 0xB6, 0xE1, 0x85, 0x5A, 0x72, 0x8C, 0x00 };
	byte goodBtoA[15] = { 0x24, 0xFD, 0x35, 0xA3, 0x5D, 0x5F, 0xB6,
	                      0x52, 0x6D, 0x32, 0xF9, 0x06, 0xDF, 0x1A, 0xC0 };
	byte AtoB[15], BtoA[15];
	int i, failed=0;

	A51_GSM(key, 64, frame, AtoB, BtoA);

	/* Compare against the test vector. */
	for (i=0; i<15; i++)
		if (AtoB[i] != goodAtoB[i])
			failed = 1;
	for (i=0; i<15; i++)
		if (BtoA[i] != goodBtoA[i])
			failed = 1;

	/* Print some debugging output. */
	printf("key: 0x");
	for (i=0; i<8; i++)
		printf("%02X", key[i]);
	printf("\n");
	printf("frame number: 0x%06X\n", (unsigned int)frame);
	printf("known good output:\n");
	printf(" A->B: 0x");
	for (i=0; i<15; i++)
		printf("%02X", goodAtoB[i]);
	printf("  B->A: 0x");
	for (i=0; i<15; i++)
		printf("%02X", goodBtoA[i]);
	printf("\n");
	printf("observed output:\n");
	printf(" A->B: 0x");
	for (i=0; i<15; i++)
		printf("%02X", AtoB[i]);
	printf("  B->A: 0x");
	for (i=0; i<15; i++)
		printf("%02X", BtoA[i]);
	printf("\n");
	
	if (!failed) {
		printf("Self-check succeeded: everything looks ok.\n");
	} else {
		/* Problems!  The test vectors didn't compare*/
		printf("\nI don't know why this broke; contact the authors.\n");
		exit(1);
	}

	printf("time test\n");
	int n = 10000;
	float t = clock();
	for (i = 0; i < n; i++) {
		A51_GSM(key, 64, frame, AtoB, BtoA);
	}
	t = (clock() - t) / (CLOCKS_PER_SEC * (float)n);
	printf("A51_GSM takes %g seconds per iteration\n", t);
}

int main(void) {
	test();
	return 0;
}
