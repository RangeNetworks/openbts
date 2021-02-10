/*
* Copyright 2011-2021 Range Networks, Inc.
*
* This software is distributed under the terms of the GNU Affero Public License.
* See the COPYING file in the main directory for details.
*
* This use of this software may be subject to additional restrictions.
* See the LEGAL file in the main directory for details.

	This program is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 2 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU Affero General Public License for more details.

	You should have received a copy of the GNU Affero General Public License
	along with this program.  If not, see <http://www.gnu.org/licenses/>.

*/


// KEEP THIS FILE CLEAN FOR GPL PUBLIC RELEASE.

#include <config.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <time.h>
#include <errno.h>
#include <arpa/inet.h>
#include <stdarg.h>
#include <string>
#include <cstring>

#define HAVE_LIBREADLINE


#ifdef HAVE_LIBREADLINE
#  include <readline/readline.h>
#  include <readline/history.h>
#endif

// Note that we ONLY use this for the name of the file to use.

// The assumption is that OpenBTS and OpenBTSCLI were built together.
#include "Globals.h"

struct sockaddr_in sa;
static char *progname = (char*) "";


char target[64] = "127.0.0.1";
int port = 49300;

static void banner()
{
	static int bannerPrinted = false;
	if (bannerPrinted) return;
	bannerPrinted = true;
	printf("OpenBTS Command Line Interface (CLI) utility\n");
	printf("Copyright 2012, 2013, 2014 Range Networks, Inc.\n");
	printf("Licensed under GPLv2.\n");
#ifdef HAVE_LIBREADLINE
	printf("Includes libreadline, GPLv2.\n");
#endif
}

static void oops(const char *fmt, ...)
{
	banner();
	va_list ap;
	va_start(ap,fmt);
	vprintf(fmt,ap);
	va_end(ap);
	printf(" OpenBTSCLI options:\n"
		"  -t ip_address : specify IP address of target machine on which OpenBTS is running\n"
		"  -p port_number : specify OpenBTS port number\n"
		"  -c command .. : execute this OpenBTS command and exit; also suppresses extraneous banners\n"
		"  -d : read one OpenBTS command, execute it and exit\n"
		"If -d or -c not specified, read OpenBTS commands and execute them.\n"
		"To see OpenBTS help, type the 'help' command to OpenBTS, or for example: OpenBTSCLI -c help\n"
		);
	exit(1);
}

bool doCmd(int fd, char *cmd) // return false to abort/exit
{
	const int bufsz = 100000;
	char resbuf[bufsz];
	int nread = 0;

	int len = strlen(cmd);
	int svlen = len;
	len = htonl(len);
	if (send(fd, &len, sizeof(len), 0) < 0) {
		perror("sending stream");
		return false;
	}
	len = svlen;
	if (send(fd, cmd, strlen(cmd), 0) < 0) {
		perror("sending stream");
		return false;
	}
	nread = recv(fd, &len, sizeof(len), 0);
	if (nread < 0) {
		perror("receiving stream");
		return false;
	}
	if (nread == 0) {
		printf("Remote connection closed\n");
		exit(1);
	}
	if (nread != (int) sizeof(len)) {
		printf("Partial read of length from server, expected %d, got %d\n", sizeof(len), len);
		exit(1);
	}
	len = ntohl(len);
	if (len >= bufsz-1) {
		printf("Response of %d bytes is too long\n", len);
		exit(1);
	}
	int off = 0;
	svlen = len;
	while(len != 0) {
		nread = recv(fd,&resbuf[off],len,0);
		if (nread < 0) {
			perror("receiving stream");
			return false;
		}
		if (nread == 0) {
			printf("Remote connection closed\n");
			exit(1);
		}
		off += nread;
		len -= nread;
	}
	nread = svlen;

	if (nread<0) {
		perror("receiving response");
		return false;
	}
	resbuf[nread] = '\0';
    if (strcmp("restart", cmd) == 0)
    {
        printf("OpenBTS has been shut down or restarted.\n");
        printf("You will need to restart OpenBTSCLI after it restarts.\n");
        return false;
    }
    if (strcmp("shutdown", cmd) == 0)
    {
        printf("OpenBTS has been shut down or restarted.\n");
        printf("You will need to restart OpenBTSCLI after it restarts.\n");
        return false;
    }
    printf("%s\n",resbuf);
    if (nread==(bufsz-1)) {
        printf("(response truncated at %d characters)\n",nread);
    }
	return true;
}

int main(int argc, char *argv[])
{
	bool isBTSDo = false;	// If set, execute one command without prompting, then exit.
	std::string sCommand("");
	progname = argv[0];
	argc--; argv++;			// Skip program name.
	while(argc > 0) {
		if (argv[0][0] == '-') {
			if (strlen(argv[0]) > 2) {
				oops("Invalid option '%s'\n", argv[0]);
				exit(1);
			}
			switch(argv[0][1]) {
				case 'd': // OpenBTSDo interface
					isBTSDo = true;
					break;
				case 'c': // Run command on command line then exit.
					isBTSDo = true;
					if (argc == 1) {
						oops("Missing argument to -c\n");
					}
					{
						// Gather up the command line.
						for (int j = 1; j < argc; j++) {
							sCommand += argv[j];
							sCommand += " ";
						}
					}
					argc = 1;	// terminates while loop.
					break;
				case 'p': // TCP Port number
					argc--, argv++;
					port = atoi(argv[0]);
					printf("TCP %d\n", port);
					break;
				case 't': // target
					argc--, argv++;
					snprintf(target, sizeof(target)-1, "%s", argv[0]);
					break;
				default:
					oops("Invalid option '%s'\n", argv[0]);
					exit(1);	// NOTREACHED but makes the compiler happy.
			}
			argc--;
			argv++;
		} else {
			oops("Invalid argument '%s'\n", argv[0]);
			exit(1);	// NOTREACHED but makes the compiler happy.
		}
	}

	if (sCommand.c_str()[0] == '\0') {
		banner();
		printf("Connecting to %s:%d...\n", target, port);
	}

	int sock = -1;
	char prompt[16] = "OpenBTS> ";

	// Define this stuff "globally" as it's needed in various places
	memset(&sa, 0, sizeof(sa));

	// the socket
	sock = socket(AF_INET,SOCK_STREAM,0);
	if (sock<0) {
		perror("opening stream socket");
		exit(1);
	}

	// destination address
	sa.sin_family = AF_INET;
	sa.sin_port = htons(port);
	if (inet_pton(AF_INET, target, &sa.sin_addr) <= 0) {
		oops("unable to convert target to an IP address\n");
	}

	if (0) {
		// (pat) I used this code for testing.
		// If you wanted to specify the port you were binding from, this is how you would do it...
		// We dont use this code - we let the connect system call pick the port.
		struct sockaddr_in sockAddrBuf;
		memset(&sockAddrBuf,0,sizeof(sockAddrBuf));		// overkill.
		sockAddrBuf.sin_family = AF_INET;
		sockAddrBuf.sin_addr.s_addr = INADDR_ANY;
		sockAddrBuf.sin_port = htons(13011);
		if (bind(sock, (struct sockaddr *) &sockAddrBuf, sizeof(struct sockaddr_in))) {	// Bind the socket to our assigned port.
			printf("bind call failed: %s",strerror(errno));
			exit(2);
		}
	}

	if (connect(sock, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
		perror("connect stream socket");
		fprintf(stderr, "Is OpenBTS running?\n");
		exit(1);
	}

#ifdef HAVE_LIBREADLINE
	char *history_name = 0;
	if (!isBTSDo)
	{
	    // start console
	    using_history();

	    static const char * const history_file_name = "/.openbts_history";
	    char *home_dir = getenv("HOME");

	    if(home_dir) {
		    size_t home_dir_len = strlen(home_dir);
		    size_t history_file_len = strlen(history_file_name);
		    size_t history_len = home_dir_len + history_file_len + 1;
		    if(history_len > home_dir_len) {
			    if(!(history_name = (char *)malloc(history_len))) {
				    perror("malloc failed");
				    exit(2);
			    }
			    memcpy(history_name, home_dir, home_dir_len);
			    memcpy(history_name + home_dir_len, history_file_name,
			       history_file_len + 1);
			    read_history(history_name);
		    }
	    }
	}
#endif


	if (!isBTSDo)
	    printf("Remote Interface Ready.\nType:\n \"help\" to see commands,\n \"version\" for version information,\n \"notices\" for licensing information,\n \"quit\" to exit console interface.\n");


        if (sCommand.c_str()[0] != '\0') {
                doCmd(sock, (char *)sCommand.c_str());
        } else
            while (1)
            {
#ifdef HAVE_LIBREADLINE
                char *cmd = readline(isBTSDo ? NULL : prompt);
                if (!cmd) continue;
                        if (cmd[0] == '\0') continue;
                if (!isBTSDo)
                    if (*cmd) add_history(cmd);
#else // HAVE_LIBREADLINE
                if (!isBTSDo)
                {
                    printf("%s",prompt);
                    fflush(stdout);
                }
                char *inbuf = (char*)malloc(BUFSIZ);
                char *cmd = fgets(inbuf,BUFSIZ-1,stdin);
                if (!cmd)
                {
                    if (isBTSDo)
                        break;
                    continue;
                }
                        if (cmd[0] == '\0') continue;
                // strip trailing CR
                cmd[strlen(cmd)-1] = '\0';
#endif
                if (!isBTSDo)
                {
                    // local quit?
                    if (strcmp(cmd,"quit")==0) {
                        printf("closing remote console\n");
                        break;
                    }
                            // shutdown via upstart
                    if (strcmp(cmd,"shutdown")==0) {
                        printf("terminating openbts\n");
                        if (getuid() == 0)
                            system("stop openbts");
                        else
                        {
                            printf("If prompted, enter the password you use for sudo\n");
                            system("sudo stop openbts");
                        }
                        break;
                    }
                    // shell escape?
                    if (cmd[0]=='!') {
                        int i = system(cmd+1);
                        if (i < 0)
                        {
                            perror("system");
                        }
                        continue;
                    }
                }
                char *pCmd = cmd;
                while(isspace(*pCmd)) pCmd++; // skip leading whitespace
                if (*pCmd)
                {
                    if (doCmd(sock, cmd) == false)
                    {
                        bool sd = false;
                        if (strcmp(cmd,"shutdown")==0)
                            sd = true;
                        else if (strcmp(cmd,"restart")==0)
                            sd = true;
                        free(cmd);
                        //{
                            if (isBTSDo)
                                break;
                            if (sd)
                                break;
                            continue;
                        //}
                    }
                }
                free(cmd);
                if (isBTSDo)
                    break;
            }

#ifdef HAVE_LIBREADLINE
	if (!isBTSDo)
	{
	    if(history_name)
	    {
		    int e = write_history(history_name);
		    if(e) {
			    fprintf(stderr, "error: history: %s\n", strerror(e));
		    }
		    free(history_name);
		    history_name = 0;
	    }
	}
#endif


	close(sock);
}
