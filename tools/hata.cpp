/*
* Copyright 2009, 2010 Free Software Foundation, Inc.
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


#include <stdlib.h>
#include <math.h>
#include <iostream>

using namespace std;

int main(int argc, char *argv[])
{
	if (argc<4) {
		cout << "Hata model path loss calculator." << endl;
		cout << "Inputs: " << endl;
		cout << argv[0] << " <freq MHz> <cableType> <mast A m> <mast B m> <dist m>" << endl;
		cout << "<cableType> is 0 for LNA @ antenna or <x> for LMR-<x>" << endl;
		cout << "Outputs: " << endl;
		cout << "cable loss" << endl;
		cout << "L_u -- urban path+cable loss" << endl;
		cout << "L_su -- suburban path+cable loss" << endl;
		cout << "L_o -- open rural path+cable loss" << endl;
		exit(0);
	}

	// Args: freq, mast ht, dist
	// Output: loss in dB

	float f = atoi(argv[1]);
	int cableType = atoi(argv[2]);
	float h_B = atoi(argv[3]);
	float h_M = atoi(argv[4]);
	float d = atoi(argv[5])/1000.0;

	cout << "f = " << f << " MHz, mast A = " << h_B << " m, mast B = " << h_M << " m, dist = " << d << " km." << endl;

	if (f<200) {
		cout << "sorry, coded for f>200 MHz" << endl;
		exit(0);
	}

	// loss in dB per m per MHz
	float lossCoeff;
	switch (cableType) {
		case 0: lossCoeff = 0.0; break;
		case 195: lossCoeff = 3.8567e-4; break;
		case 400: lossCoeff = 1.35e-4; break;
		case 600: lossCoeff = 8.691e-5; break;
		case 900: lossCoeff = 5.896e-5; break;
		default:
			cerr << "unsupported cable type LMR-" << cableType << endl;
			exit(1);
	}

	// Cable loss
	float cableLoss = lossCoeff * h_B * f;
	cout << "cable loss at mast A is " << cableLoss << " dB" << endl;

	float lh_M = log10(11.75*h_M);
	float C_H = 3.2*lh_M*lh_M-4.97;
	//cout << "C_H = " << C_H << " dB." << endl;

	float logf = log10(f);

	float L_u = 69.55 + 26.16*logf - 13.82*log10(h_B) - C_H + (44.9 - 6.55*log10(h_B))*log10(d);
	L_u += cableLoss;
	cout << "Urban: L_u = " << L_u << " dB." << endl;

	float logf_28 = log10(f/28);
	float L_su = L_u - 2*logf_28*logf_28 - 5.4;
	L_su += cableLoss;
	cout << "Suburban: L_su = " << L_su << " dB." << endl;

	float L_o = L_u - 4.78*logf*logf + 18.33*logf - 40.94;
	L_o += cableLoss;
	cout << "Open: L_o = " << L_o << " dB." << endl;
}
