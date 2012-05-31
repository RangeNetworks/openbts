/*
* Copyright 2011 Range Networks, Inc.
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


// KEEP THIS FILE CLEAN FOR GPL PUBLIC RELEASE.

#include <sys/types.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>


#define DEFAULT_CMD_PATH "/var/run/command"

int main(int argc, char *argv[])
{
	const char* cmdPath = DEFAULT_CMD_PATH;
	if (argc!=1) {
		cmdPath = argv[1];
	}

	char rspPath[200];
	sprintf(rspPath,"/tmp/OpenBTS.do.%d",getpid());

	// the socket
	int sock = socket(AF_UNIX,SOCK_DGRAM,0);
	if (sock<0) {
		perror("opening datagram socket");
		exit(1);
	}

	// destination address
	struct sockaddr_un cmdSockName;
	cmdSockName.sun_family = AF_UNIX;
	strcpy(cmdSockName.sun_path,cmdPath);

	// locally bound address
	struct sockaddr_un rspSockName;
	rspSockName.sun_family = AF_UNIX;
	strcpy(rspSockName.sun_path,rspPath);
	if (bind(sock, (struct sockaddr *) &rspSockName, sizeof(struct sockaddr_un))) {
		perror("binding name to datagram socket");
		exit(1);
	}


	char *inbuf = (char*)malloc(200);
	char *cmd = fgets(inbuf,199,stdin);
	if (!cmd) exit(0);

	if (sendto(sock,cmd,strlen(cmd)+1,0,(struct sockaddr*)&cmdSockName,sizeof(cmdSockName))<0) {
		perror("sending datagram");
		exit(1);
	}

	const int bufsz = 1500;
	char resbuf[bufsz];
	int nread = recv(sock,resbuf,bufsz-1,0);
	if (nread<0) {
		perror("receiving response");
		exit(1);
	}
	resbuf[nread] = '\0';
	printf("%s\n",resbuf);

	close(sock);
}
