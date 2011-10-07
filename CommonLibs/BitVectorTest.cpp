/*
* Copyright 2008 Free Software Foundation, Inc.
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
#include <iostream>
#include <cstdlib>
 
using namespace std;


int main(int argc, char *argv[])
{
	BitVector v1("0000111100111100101011110000");
	cout << v1 << endl;
	v1.LSB8MSB();
	cout << v1 << endl;
	ViterbiR2O4 vCoder;
	BitVector v2(v1.size()*2);
	v1.encode(vCoder,v2);
	cout << v2 << endl;
	SoftVector sv2(v2);
	cout << sv2 << endl;
	for (unsigned i=0; i<sv2.size()/4; i++) sv2[random()%sv2.size()]=0.5;
	cout << sv2 << endl;
	BitVector v3(v1.size());
	sv2.decode(vCoder,v3);
	cout << v3 << endl;

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
	mCS.decode(vCoder,mU);
	cout << "c=" << mCS << endl;
	cout << "u=" << mU << endl;


	unsigned char ts[9] = "abcdefgh";
	BitVector tp(70);
	cout << "ts=" << ts << endl;
	tp.unpack(ts);
	cout << "tp=" << tp << endl;
	tp.pack(ts);
	cout << "ts=" << ts << endl;
}
