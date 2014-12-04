#!/bin/bash

# copy of debian/postinst
DATE=$(date --rfc-3339='date')
CONFIG_BACKUP=/etc/OpenBTS/OpenBTS.dump-$DATE

# add user openbts
if ! getent passwd openbts > /dev/null ; then
        echo 'Adding system user for OpenBTS' 1>&2
        adduser --system --group --quiet \
                --home /home/openbts \
                --gecos "OpenBTS daemon" \
                openbts
fi

# add user openbts to some groups
for group in sudo www-data; do
        if groups openbts | grep -w -q -v $group; then
                adduser openbts $group 
        fi
done

if [ ! -e $CONFIG_BACKUP ]; then
        sqlite3 /etc/OpenBTS/OpenBTS.db ".dump" > $CONFIG_BACKUP
fi

sqlite3 /etc/OpenBTS/OpenBTS.db ".read /etc/OpenBTS/OpenBTS.example.sql" > /dev/null 2>&1

chown openbts:openbts /home/openbts/openbtsconfig

