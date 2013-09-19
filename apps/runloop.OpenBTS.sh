#!/bin/sh

# A script to restart and just keep OpenBTS running.
while true;
do
	# (pat) Do not remove this 'killall OpenBTS'.  If removed, the second and later iterations of this loop may
	# be unable to start GPRS service.  The OpenBTS.log will include: ggsn: ERROR: Could not open tun device sgsntun
	killall OpenBTS
	killall wget;
	sleep 1;
	./OpenBTS;
done
