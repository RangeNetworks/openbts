/*
* Copyright 2010 Free Software Foundation, Inc.
* Copyright (c) 2008, 2010 Kestrel Signal Processing, Inc.
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

/*
Contributors:
Harvind S. Samra, hssamra@kestrelsp.com
*/


#include "sigProcLib.h"
//#include "radioInterface.h"
#include <Logger.h>
#include <Configuration.h>

using namespace std;

ConfigurationTable gConfig;

int main(int argc, char **argv) {

  gLogInit("sigProcLibTest","DEBUG");

  int samplesPerSymbol = 1;

  int TSC = 2;

  sigProcLibSetup(samplesPerSymbol);
  
  signalVector *gsmPulse = generateGSMPulse(2,samplesPerSymbol);
  cout << *gsmPulse << endl;

  signalVector duh(600);
  duh.fill(1.0);
  frequencyShift(&duh,&duh,-2.0*M_PI*(400.0/1083.0));

  cout << duh;

  exit(1);

  BitVector RACHBurstStart = "01010101";
  BitVector RACHBurstRest = "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000";

  BitVector RACHBurst(BitVector(RACHBurstStart,gRACHSynchSequence),RACHBurstRest);
 

  signalVector *RACHSeq = modulateBurst(RACHBurst,
                                        *gsmPulse,
                                        9,
                                        samplesPerSymbol);

  generateRACHSequence(*gsmPulse,samplesPerSymbol);

  complex a; float t;
  detectRACHBurst(*RACHSeq, 5, samplesPerSymbol,&a,&t); 

  //cout << *RACHSeq << endl;
  //signalVector *autocorr = correlate(RACHSeq,RACHSeq,NULL,NO_DELAY);

  //cout << *autocorr;

  //exit(1);
 

  /*signalVector x(6500);
  x.fill(1.0);

  frequencyShift(&x,&x,0.48*M_PI);

  signalVector *y = polyphaseResampleVector(x,96,65,NULL);

  cout << *y << endl;
 
  exit(1);*/

  //CommSig normalBurstSeg = "0000000000000000000000000000000000000000000000000000000000000";

  BitVector normalBurstSeg = "0000101010100111110010101010010110101110011000111001101010000";

  BitVector normalBurst(BitVector(normalBurstSeg,gTrainingSequence[TSC]),normalBurstSeg);


  generateMidamble(*gsmPulse,samplesPerSymbol,TSC);


  signalVector *modBurst = modulateBurst(normalBurst,*gsmPulse,
                                         0,samplesPerSymbol);

  
  //delayVector(*rsVector2,6.932);

  complex ampl = 1;
  float TOA = 0;

  //modBurst = rsVector2;
  //delayVector(*modBurst,0.8);

  /*
  signalVector channelResponse(4);
  signalVector::iterator c=channelResponse.begin();
  *c = (complex) 9000.0; c++;
  *c = (complex) 0.4*9000.0; c++; c++;
  *c = (complex) -1.2*0;

  signalVector *guhBurst = convolve(modBurst,&channelResponse,NULL,NO_DELAY);
  delete modBurst; modBurst = guhBurst;
  */

  signalVector *chanResp;
  /*
  double noisePwr = 0.001/sqrtf(2);
  signalVector *noise = gaussianNoise(modBurst->size(),noisePwr);
  */
  float chanRespOffset;
  analyzeTrafficBurst(*modBurst,TSC,8.0,samplesPerSymbol,&ampl,&TOA,1,true,&chanResp,&chanRespOffset);
  //addVector(*modBurst,*noise);

  cout << "ampl:" << ampl << endl;
  cout << "TOA: " << TOA << endl;
  //cout << "chanResp: " << *chanResp << endl;
  SoftVector *demodBurst = demodulateBurst(*modBurst,*gsmPulse,samplesPerSymbol,(complex) ampl, TOA);
  
  cout << *demodBurst << endl;

  /*
  COUT("chanResp: " << *chanResp);

  signalVector *w,*b;
  designDFE(*chanResp,1.0/noisePwr,7,&w,&b); 
  COUT("w: " << *w);
  COUT("b: " << *b);

 
  SoftSig *DFEBurst = equalizeBurst(*modBurst,TOA-chanRespOffset,samplesPerSymbol,*w,*b);
  COUT("DFEBurst: " << *DFEBurst);

  delete gsmPulse;
  delete RACHSeq;
  delete modBurst;
  delete sendLPF;
  delete rcvLPF;
  delete rsVector;
  //delete rsVector2;
  delete autocorr;
  delete chanResp;
  delete noise;
  delete demodBurst;
  delete w;
  delete b;
  delete DFEBurst;  
  */

  sigProcLibDestroy();

}
