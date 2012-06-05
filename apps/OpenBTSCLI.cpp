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

#include <config.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <time.h>

#ifdef HAVE_LIBREADLINE
#  include <readline/readline.h>
#  include <readline/history.h>
#endif


#define DEFAULT_CMD_PATH "/var/run/command"
#define DEFAULT_RSP_PATH "./response"


int main(int argc, char *argv[])
{
	const char* cmdPath = DEFAULT_CMD_PATH;
	if (argc!=1) {
		cmdPath = argv[1];
	}

	char rspPath[200];
	sprintf(rspPath,"/tmp/OpenBTS.console.%d.%8lx",getpid(),time(NULL));


	printf("command socket path is %s\n", cmdPath);

	char prompt[strlen(cmdPath) + 20];
	sprintf(prompt,"OpenBTS> ");

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
	char rmcmd[strlen(rspPath)+5];
	sprintf(rmcmd,"rm %s",rspPath);
	system(rmcmd);
	strcpy(rspSockName.sun_path,rspPath);
	if (bind(sock, (struct sockaddr *) &rspSockName, sizeof(struct sockaddr_un))) {
		perror("binding name to datagram socket");
		exit(1);
	}
	printf("response socket bound to %s\n",rspSockName.sun_path);

#ifdef HAVE_LIBREADLINE
	// start console
	using_history();

	static const char * const history_file_name = "/.openbts_history";
	char *history_name = 0;
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

	printf("readline installed\n");
#endif


	printf("Remote Interface Ready.\nType:\n \"help\" to see commands,\n \"version\" for version information,\n \"notices\" for licensing information.\n\"quit\" to exit console interface\n");


	while (1) {

#ifdef HAVE_LIBREADLINE
		char *cmd = readline(prompt);
		if (!cmd) break;
		if (*cmd) add_history(cmd);
#else // HAVE_LIBREADLINE
		printf("%s",prompt);
		fflush(stdout);
		char *inbuf = (char*)malloc(200);
		char *cmd = fgets(inbuf,199,stdin);
		if (!cmd) continue;
		// strip trailing CR
		cmd[strlen(cmd)-1] = '\0';
#endif

		// local quit?
		if (strcmp(cmd,"quit")==0) {
			printf("closing remote console\n");
			break;
		}
		if (sendto(sock,cmd,strlen(cmd)+1,0,(struct sockaddr*)&cmdSockName,sizeof(cmdSockName))<0) {
			perror("sending datagram");
			printf("Is the remote application running?\n");
			continue;
		}
		free(cmd);
		const int bufsz = 10000;
		char resbuf[bufsz];
		int nread = recv(sock,resbuf,bufsz-1,0);
		if (nread<0) {
			perror("receiving response");
			continue;
		}
		resbuf[nread] = '\0';
		printf("%s\n",resbuf);
		if (nread==(bufsz-1)) printf("(response truncated at %d characters)\n",nread);
	}

#ifdef HAVE_LIBREADLINE
	if(history_name) {
		int e = write_history(history_name);
		if(e) {
			fprintf(stderr, "error: history: %s\n", strerror(e));
		}
		free(history_name);
		history_name = 0;
	}
#endif


	close(sock);
}
