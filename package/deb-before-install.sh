#!/bin/bash

# copy of debian/preinst
INSTALL_DIR=/OpenBTS
DATE=$(date --rfc-3339='date')
BACKUP_DIR=$INSTALL_DIR/backup_$DATE

if [ -f $INSTALL_DIR/OpenBTS -a -f $INSTALL_DIR/transceiver ]; then
    if [ ! -e $BACKUP_DIR ]; then
        mkdir -p $BACKUP_DIR/

        mv $INSTALL_DIR/OpenBTS $BACKUP_DIR/
        mv $INSTALL_DIR/transceiver $BACKUP_DIR/
    fi
fi
