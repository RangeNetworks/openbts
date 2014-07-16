#!/bin/sh

#
# Copyright 2014 Range Networks, Inc.
#
# This software is distributed under the terms of the GNU Public License.
# See the COPYING file in the main directory for details.
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.
#

cd $1

INFO=""
if [ -d ./.svn ]; then
	INFO="r$(svn info . | grep "Last Changed Rev:" | cut -d " " -f 4) CommonLibs:r$(svn info ./CommonLibs | grep "Last Changed Rev:" | cut -d " " -f 4)"
elif [ -d ./.git ]; then
	INFO="$(git rev-parse --short=10 HEAD) CommonLibs:$(cd CommonLibs; git rev-parse --short=10 HEAD)"
fi

echo $INFO
