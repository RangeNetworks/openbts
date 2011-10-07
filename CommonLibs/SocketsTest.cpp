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




#include "Sockets.h"
#include "Threads.h"
#include <stdio.h>
#include <stdlib.h>


static const int gNumToSend = 10;


void *testReaderIP(void *)
{
	UDPSocket readSocket(5934, "localhost", 5061);
	readSocket.nonblocking();
	int rc = 0;
	while (rc<gNumToSend) {
		char buf[MAX_UDP_LENGTH];
		int count = readSocket.read(buf);
		if (count>0) {
			COUT("read: " << buf);
			rc++;
		} else {
			sleep(2);
		}
	}
	return NULL;
}



void *testReaderUnix(void *)
{
	UDDSocket readSocket("testDestination");
	readSocket.nonblocking();
	int rc = 0;
	while (rc<gNumToSend) {
		char buf[MAX_UDP_LENGTH];
		int count = readSocket.read(buf);
		if (count>0) {
			COUT("read: " << buf);
			rc++;
		} else {
			sleep(2);
		}
	}
	return NULL;
}


int main(int argc, char * argv[] )
{

  Thread readerThreadIP;
  readerThreadIP.start(testReaderIP,NULL);
  Thread readerThreadUnix;
  readerThreadUnix.start(testReaderUnix,NULL);

  UDPSocket socket1(5061, "127.0.0.1",5934);
  UDDSocket socket1U("testSource","testDestination");
  
  COUT("socket1: " << socket1.port());

  // give the readers time to open
  sleep(1);

  for (int i=0; i<gNumToSend; i++) {
    socket1.write("Hello IP land");	
	socket1U.write("Hello Unix domain");
	sleep(1);
  }

  readerThreadIP.join();
  readerThreadUnix.join();
}

// vim: ts=4 sw=4
