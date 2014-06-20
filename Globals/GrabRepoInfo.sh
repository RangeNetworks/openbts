#!/bin/sh
cd $1

INFO=""
if [ -d ./.svn ]; then
	INFO="r$(svn info . | grep "Last Changed Rev:" | cut -d " " -f 4) CommonLibs:r$(svn info ./CommonLibs | grep "Last Changed Rev:" | cut -d " " -f 4)"
elif [ -d ./.git ]; then
	INFO="$(git rev-parse --short=10 HEAD) CommonLibs:$(cd CommonLibs; git rev-parse --short=10 HEAD)"
fi

echo $INFO
