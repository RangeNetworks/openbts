#
# Copyright 2008 Free Software Foundation, Inc.
# Copyright 2011 Range Networks, Inc.
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

include $(top_srcdir)/Makefile.common

DESTDIR := 

# These variables are defined in ../Makefile.common
# AM_CPPFLAGS = $(STD_DEFINES_AND_INCLUDES)
# AM_CXXFLAGS = -Wall -ldl -pthread

noinst_PROGRAMS = \
	OpenBTS \
	OpenBTSDo \
	OpenBTSCLI

OpenBTS_SOURCES = OpenBTS.cpp GetConfigurationKeys.cpp
OpenBTS_CPPFLAGS = $(STD_DEFINES_AND_INCLUDES)

ourlibs = \
	$(CLI_LA) \
	$(SIP_LA) \
	$(GSM_LA) \
	$(GPRS_LA) \
	$(SGSNGGSN_LA) \
	$(TRX_LA) \
	$(GLOBALS_LA) \
	$(CONTROL_LA) \
	$(SR_LA) \
	$(COMMON_LA) \
	$(SQLITE_LA) \
	$(SMS_LA) \
	$(PEERING_LA) \
	$(OSIP_LIBS) \
	$(ORTP_LIBS)

OpenBTS_LDADD = $(ourlibs) -ldl -losipparser2 -losip2 -lortp -la53

OpenBTSCLI_SOURCES = OpenBTSCLI.cpp
# RedHat RHEL5 needs to have ncurses included
OpenBTSCLI_LDADD = -lreadline -lncurses
OpenBTSDo_SOURCES = OpenBTSDo.cpp

EXTRA_DIST = \
	OpenBTS.example.sql \
	setUpFiles.sh \
	runloop.OpenBTS.sh \
	generateConfigTable.sh \
	exportConfigTable.sh \
	importConfigTable.sh \
	generateKeys.sh \
	verifyKey.sh

install: OpenBTS OpenBTSCLI OpenBTSDo
	mkdir -p "$(DESTDIR)/OpenBTS/"
	install OpenBTS "$(DESTDIR)/OpenBTS/"
	install OpenBTSCLI "$(DESTDIR)/OpenBTS/"
	install OpenBTSDo "$(DESTDIR)/OpenBTS/"
	install runloop.OpenBTS.sh "$(DESTDIR)/OpenBTS/"
	mkdir -p "$(DESTDIR)/etc/OpenBTS/"
	install iptables.rules "$(DESTDIR)/etc/OpenBTS/"
	install OpenBTS.example.sql "$(DESTDIR)/etc/OpenBTS/"
	mkdir -p "$(DESTDIR)/etc/rsyslog.d/"
	mkdir -p "$(DESTDIR)/etc/logrotate.d/"
	install rsyslogd.OpenBTS.conf "$(DESTDIR)/etc/rsyslog.d/OpenBTS.conf"
	install OpenBTS.logrotate "$(DESTDIR)/etc/logrotate.d/OpenBTS"
