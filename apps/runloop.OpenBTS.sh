#!/bin/sh

# A script to restart and just keep OpenBTS running.
while true; do killall transceiver; killall wget; sleep 2; ./OpenBTS; done
