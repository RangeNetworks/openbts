/*
* Copyright 2008, 2010 Free Software Foundation, Inc.
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



#include <unistd.h>
#include <fcntl.h>
#include <cstdio>
#include <sys/select.h>

#include "Threads.h"
#include "Sockets.h"
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>

#include <string.h>
#include <stdlib.h>


//mutex for protecting non-thread safe gethostbyname
static Mutex sgGethostbynameLock;

bool resolveAddress(struct sockaddr_in *address, const char *hostAndPort)
{
	assert(address);
	assert(hostAndPort);
	char *copy = strdup(hostAndPort);
	char *colon = strchr(copy,':');
	if (!colon) return false;
	*colon = '\0';
	char *host = copy;
	unsigned port = strtol(colon+1,NULL,10);
	bool retVal = resolveAddress(address,host,port);
	free(copy);
	return retVal;
}

bool resolveAddress(struct sockaddr_in *address, const char *host, unsigned short port)
{
	assert(address);
	assert(host);
	//gethostbyname not thread safe
	ScopedLock lock(sgGethostbynameLock);
	// FIXME -- Need to ignore leading/trailing spaces in hostname.
	struct hostent *hp = gethostbyname(host);
	if (hp==NULL) {
		CERR("WARNING -- gethostbyname() failed for " << host << ", " << hstrerror(h_errno));
		return false;
	}
	address->sin_family = AF_INET;
	bcopy(hp->h_addr, &(address->sin_addr), hp->h_length);
	address->sin_port = htons(port);
	return true;
}



DatagramSocket::DatagramSocket()
{
	bzero(mDestination,sizeof(mDestination));
}





void DatagramSocket::nonblocking()
{
	fcntl(mSocketFD,F_SETFL,O_NONBLOCK);
}

void DatagramSocket::blocking()
{
	fcntl(mSocketFD,F_SETFL,0);
}

void DatagramSocket::close()
{
	::close(mSocketFD);
}


DatagramSocket::~DatagramSocket()
{
	close();
}





int DatagramSocket::write( const char * message, size_t length )
{
	assert(length<=MAX_UDP_LENGTH);
	int retVal = sendto(mSocketFD, message, length, 0,
		(struct sockaddr *)mDestination, addressSize());
	if (retVal == -1 ) perror("DatagramSocket::write() failed");
	return retVal;
}

int DatagramSocket::writeBack( const char * message, size_t length )
{
	assert(length<=MAX_UDP_LENGTH);
	int retVal = sendto(mSocketFD, message, length, 0,
		(struct sockaddr *)mSource, addressSize());
	if (retVal == -1 ) perror("DatagramSocket::write() failed");
	return retVal;
}



int DatagramSocket::write( const char * message)
{
	size_t length=strlen(message)+1;
	return write(message,length);
}

int DatagramSocket::writeBack( const char * message)
{
	size_t length=strlen(message)+1;
	return writeBack(message,length);
}



int DatagramSocket::send(const struct sockaddr* dest, const char * message, size_t length )
{
	assert(length<=MAX_UDP_LENGTH);
	int retVal = sendto(mSocketFD, message, length, 0, dest, addressSize());
	if (retVal == -1 ) perror("DatagramSocket::send() failed");
	return retVal;
}

int DatagramSocket::send(const struct sockaddr* dest, const char * message)
{
	size_t length=strlen(message)+1;
	return send(dest,message,length);
}





int DatagramSocket::read(char* buffer)
{
	socklen_t temp_len = sizeof(mSource);
	int length = recvfrom(mSocketFD, (void*)buffer, MAX_UDP_LENGTH, 0,
	    (struct sockaddr*)&mSource,&temp_len);
	if ((length==-1) && (errno!=EAGAIN)) {
		perror("DatagramSocket::read() failed");
		throw SocketError();
	}
	return length;
}


int DatagramSocket::read(char* buffer, unsigned timeout)
{
	fd_set fds;
	FD_SET(mSocketFD,&fds);
	struct timeval tv;
	tv.tv_sec = timeout/1000;
	tv.tv_usec = (timeout%1000)*1000;
	int sel = select(mSocketFD+1,&fds,NULL,NULL,&tv);
	if (sel<0) {
		perror("DatagramSocket::read() select() failed");
		throw SocketError();
	}
	if (sel==0) return -1;
	return read(buffer);
}






UDPSocket::UDPSocket(unsigned short wSrcPort)
	:DatagramSocket()
{
	open(wSrcPort);
}


UDPSocket::UDPSocket(unsigned short wSrcPort,
          	 const char * wDestIP, unsigned short wDestPort )
	:DatagramSocket()
{
	open(wSrcPort);
	destination(wDestPort, wDestIP);
}



void UDPSocket::destination( unsigned short wDestPort, const char * wDestIP )
{
	resolveAddress((sockaddr_in*)mDestination, wDestIP, wDestPort );
}


void UDPSocket::open(unsigned short localPort)
{
	// create
	mSocketFD = socket(AF_INET,SOCK_DGRAM,0);
	if (mSocketFD<0) {
		perror("socket() failed");
		throw SocketError();
	}

	// bind
	struct sockaddr_in address;
	size_t length = sizeof(address);
	bzero(&address,length);
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = INADDR_ANY;
	address.sin_port = htons(localPort);
	if (bind(mSocketFD,(struct sockaddr*)&address,length)<0) {
		perror("bind() failed");
		throw SocketError();
	}
}



unsigned short UDPSocket::port() const
{
	struct sockaddr_in name;
	socklen_t nameSize = sizeof(name);
	int retVal = getsockname(mSocketFD, (struct sockaddr*)&name, &nameSize);
	if (retVal==-1) throw SocketError();
	return ntohs(name.sin_port);
}





UDDSocket::UDDSocket(const char* localPath, const char* remotePath)
	:DatagramSocket()
{
	if (localPath!=NULL) open(localPath);
	if (remotePath!=NULL) destination(remotePath);
}



void UDDSocket::open(const char* localPath)
{
	// create
	mSocketFD = socket(AF_UNIX,SOCK_DGRAM,0);
	if (mSocketFD<0) {
		perror("socket() failed");
		throw SocketError();
	}

	// bind
	struct sockaddr_un address;
	size_t length = sizeof(address);
	bzero(&address,length);
	address.sun_family = AF_UNIX;
	strcpy(address.sun_path,localPath);
	unlink(localPath);
	if (bind(mSocketFD,(struct sockaddr*)&address,length)<0) {
		perror("bind() failed");
		throw SocketError();
	}
}



void UDDSocket::destination(const char* remotePath)
{
	struct sockaddr_un* unAddr = (struct sockaddr_un*)mDestination;
	strcpy(unAddr->sun_path,remotePath);
}




// vim:ts=4:sw=4
