#!/bin/sh

CONFIG_DIR=/etc/OpenBTS

# Create the config dir if it dos not exist
if [ ! -d "$CONFIG_DIR" ]; then
        mkdir $CONFIG_DIR
fi

# Create the config/saved dir if it dos not exist
if [ ! -d "$CONFIG_DIR/saved" ]; then
        mkdir $CONFIG_DIR/saved
fi

# backup any exsisting DB before we create the default
if [ -e $CONFIG_DIR/OpenBTS.db ]; then
	mv -f $CONFIG_DIR/OpenBTS.db $CONFIG_DIR/saved/OpenBTS.db
fi
sqlite3 $CONFIG_DIR/OpenBTS.db ".read OpenBTS.export.sql"
