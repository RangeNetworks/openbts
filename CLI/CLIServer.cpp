/*
* Copyright 2014 Range Networks, Inc.
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

#include <stdio.h>
#include <stdlib.h>
#include "CLI.h"
#include <Globals.h>
#include <Configuration.h>

using namespace CommandLine;

void Parser::Prompt() const
{
    printf("%s> ",this->mCommandName.c_str()); fflush(stdout);
}

int Parser::Execute(bool console, const char cmdbuf[], int outfd)
{
    // step 2 - execute
    gReports.incr("OpenBTS.CLI.Command");
    const char *type = console ? "Console: " : "Socket: ";
    LOG(INFO) << type << "received command \"" << cmdbuf << "\"";
    std::ostringstream sout;
    int res = this->process(cmdbuf,sout);
    const std::string rspString= sout.str();
    const char* rsp = rspString.c_str();

    // step 3 - respond, put length at start of response
    LOG(INFO) << type << "sending " << strlen(rsp) << "-char result";
    int len = strlen(rsp);
    if (console)
    {
        if (write(outfd,rsp,len) != len) {
            LOG(ERR) << type << "can't send CLI response";
            gReports.incr("OpenBTS.CLI.Command.ResponseFailure");
        }
        this->Prompt();
    } else
    {
        len = htonl(len);
        int sendLen = sizeof(len);
        int sentLen = 0;
        if ((sentLen = send(outfd, &len, sendLen, 0)) != sendLen)
        {
            LOG(ERR) << type << "can't send CLI response";
            gReports.incr("OpenBTS.CLI.Command.ResponseFailure");
        } else
        {
            sendLen = strlen(rsp);
            if ((sentLen = send(outfd,rsp,sendLen,0)) != sendLen) {
                LOG(ERR) << type << "can't send CLI response";
                gReports.incr("OpenBTS.CLI.Command.ResponseFailure");
            }
        }
    }
    return res;
}


// (pat) 4-2014: Moved this code written by Dave G. from OpenBTS.cpp to this directory so that it can be shared by multiple apps.
// I did not bother to rename all the gReports variables.
void Parser::cliServer()
{
	assert(mCommandName.size());	// Dont leave this empty.

	int netSockFd = -1;

	// CLI Interface code
	string CLIPortOpt = "CLI.Port", CLIInterfaceOpt = "CLI.Interface";
	if (mCommandName != "OpenBTS") {
		CLIPortOpt = mCommandName + "." + CLIPortOpt;
		CLIInterfaceOpt = mCommandName + "." + CLIInterfaceOpt;
	}
	string netPort = gConfig.getStr(CLIPortOpt);
	string neInterface = gConfig.defines(CLIInterfaceOpt) ? gConfig.getStr(CLIInterfaceOpt) : string(""); // TODO: Future implementation
	bool netTcp = false;

	if (netPort.size())
	{
		in_addr_t iInterface = neInterface.size() == 0 ? inet_addr("127.0.0.1") : inet_addr(neInterface.c_str());
		// printf("Interface mask 0x%08X\n", iInterface);
		struct sockaddr_in servAddr;
		memset(&servAddr, 0, sizeof(servAddr));
		servAddr.sin_family = AF_INET;
		servAddr.sin_addr.s_addr = iInterface;
		int port = 0;
		netTcp = true;
		port = atoi(netPort.c_str());
		servAddr.sin_port = htons(port);

		netSockFd = socket(AF_INET,SOCK_STREAM,0);
		if (netSockFd<0) {
			perror("creating CLI network stream socket");
			LOG(ALERT) << "cannot create network tcp socket for CLI";
			gReports.incr("OpenBTS.Exit.CLI.Socket");
			exit(1);
		}
		int optval = 1;
		setsockopt(netSockFd, SOL_SOCKET,SO_REUSEADDR, &optval, sizeof(optval));
		if (bind(netSockFd, (struct sockaddr *) &servAddr, sizeof(struct sockaddr_in))) {
			perror("binding name to network socket");
			LOG(ALERT) << "cannot bind socket for CLI at " << (netTcp ? "tcp:" : "udp:" ) << netPort;
			gReports.incr("OpenBTS.Exit.CLI.Socket");
			exit(1);
		}
		if (netTcp) listen(netSockFd, 10);
	}

	if (netSockFd != -1)
	{
		char buf[BUFSIZ];
		snprintf(buf, sizeof(buf)-1, "OpenBTSCLI network socket support for %s:%s\n", ( netTcp ? "tcp" : "udp") , netPort.c_str());
		COUT(buf);
	}

	fd_set rdFds, curFds;
	FD_ZERO(&rdFds);
	if (netSockFd != -1) FD_SET(netSockFd, &rdFds);
	if (isatty(0)) // if not running from a terminal, don't bother
	{
    FD_SET(0, &rdFds); // console
    this->Prompt();
	}
	char cmdbuf[BUFSIZ];
	bool isRunning = true;
	struct timeval tv;
	while (isRunning) {
		// First, build mask of possible input channels
		curFds = rdFds;
		tv.tv_sec = 60; tv.tv_usec = 0;
		int selRet = select(FD_SETSIZE, &curFds, NULL, NULL, &tv);
		if (selRet > 0)
		{
			for (int i = 0; i < FD_SETSIZE; i++)
			{
				if (!FD_ISSET(i, &curFds))
					continue;
                if (i == 0)
                {
                    if (NULL == fgets(cmdbuf, sizeof(cmdbuf)-1, stdin)) { continue; }
                    char *p = strchr(cmdbuf, '\n');
                    if (p != NULL)
                        *p = '\0';
                    else
                        cmdbuf[BUFSIZ-1] = '\0';
                    int res = Execute(true, cmdbuf, 1); // and ignore the result
                    if (res < 0) { isRunning = false; }
                } else if (i == netSockFd) // accept (tcp)
				{
					if (netTcp)
					{
						struct sockaddr peer;
						socklen_t len = sizeof(peer);
						// int fd = accept(i, NULL, NULL); // don't care who it's from
						int fd = accept(i, &peer, &len);	// (pat) On the other hand, might be interesting to know who it's from.
						if (fd < 0) {
							LOG(ERR) << "can't accept network stream connection";
							gReports.incr("OpenBTS.CLI.Command.ResponseFailure");
							break;
						}
						// (pat) Could make an argument that we should always print this message as a security measure,
						// since the peer is allowed complete access to OpenBTS.
						if (1) {
							// Lets print out who it was from:
							char addrString[256];
							struct sockaddr_in *sp = (struct sockaddr_in*)&peer;
							if (const char *ret = inet_ntop(AF_INET,&(sp->sin_addr),addrString,255)) {
								LOG(INFO) << format("Accepting CLI connection from %s:%d", ret,(int)ntohs(sp->sin_port));
							}
						}
						FD_SET(fd, &rdFds); // so that we can select on it next time around
					}
				} else if (FD_ISSET(i, &curFds)) // a tcp socket that had previously been accepted
				{
					// step 1 - read the command.  Format is <len>data, because it may take several reads to get the full read
					//			along a stream.
					int len;
					int nread = recv(i, &len, sizeof(len), 0);
					if (nread < 0)
					{
						LOG(ERR) << "can't read CLI request from stream";
						gReports.incr("OpenBTS.CLI.Command.ResponseFailure");
						break;
					}
					if (len == 0) // clsoe
					{
						FD_CLR(i, &rdFds);
						shutdown(i, SHUT_RDWR);
						close(i);
						continue; // go to next socket
					}
					if (len < (int) sizeof(len)) // should never get here
					{
						char buf[BUFSIZ];
						sprintf(buf, "Unable to read complete length, s.b. %d bytes, got %d bytes\n", sizeof(len), len);
						LOG(ERR) << buf;
						gReports.incr("OpenBTS.CLI.Command.ResponseFailure");
						break;
					}
					len = ntohl(len);
					int off = 0;
					while(len != 0)
					{
						nread = recv(i, &cmdbuf[off], len, 0);
						if (nread < 0)
						{
							LOG(ERR) << "can't read CLI request from stream";
							gReports.incr("OpenBTS.CLI.Command.ResponseFailure");
							break;
						}
						if (nread == 0) // close
						{
							FD_CLR(i, &rdFds);
							shutdown(i, SHUT_RDWR);
							close(i);
							continue; // go to next socket
						}
						off += nread;
						len -= nread;
					}
					cmdbuf[off] = '\0';

                    int res = Execute(false, cmdbuf, i);
                    // (comment by Dave G) NOTE: This change was made because we need to exit as
                    // quickly as possible, or we end up getting some other
                    // threads faulting as objects vanish from under them.
					// (pat 3-2014) I dont think the above helped.  But I had to fix up the exit routines in order to run gprof,
					// so it is ok to exit normally now.
                    if (res < 0) {
						isRunning = false;
						break;
					}
				}
			}
		} else if (selRet < 0) {
			// (pat) TEMPORARY!  Watch this to try to figure out where the unexpected signal is coming from.
			perror("system call during CLI select loop");
		}
	}

	if (netSockFd != -1) close(netSockFd);	// Why bother?
}
