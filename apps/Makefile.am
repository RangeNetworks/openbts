#
# Copyright 2008 Free Software Foundation, Inc.
# Copyright 2011, 2014 Range Networks, Inc.
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
	JSONEventsClient \
	OpenBTS \
	OpenBTSCLI

OpenBTS_SOURCES = OpenBTS.cpp GetConfigurationKeys.cpp
OpenBTS_CPPFLAGS = $(STD_DEFINES_AND_INCLUDES)

ourlibs = \
	$(CLI_LA) \
	$(SIP_LA) \
	$(GSM_LA) \
	$(GSMSHARE_LA) \
	$(GPRS_LA) \
	$(SGSNGGSN_LA) \
	$(TRX_LA) \
	$(GLOBALS_LA) \
	$(CONTROL_LA) \
	$(COMMON_LA) \
	$(SMS_LA) \
	$(PEERING_LA) \
	$(NODEMANAGER_LA) \
	$(ORTP_LIBS)

clilibs= \
	$(GLOBALS_LA)

OpenBTS_LDADD = $(ourlibs) -lcrypto -lssl -ldl -lortp -la53 -lcoredumper 
OpenBTS_LDFLAGS = $(GPROF_OPTIONS) -rdynamic
noinst_HEADERS = \
	OpenBTSConfig.h

OpenBTSCLI_SOURCES = OpenBTSCLI.cpp
OpenBTSCLI_LDADD = $(clilibs) -lreadline
OpenBTSCLI_CXXFLAGS = -DBUILD_CLI

JSONEventsClient_SOURCES = JSONEventsClient.cpp
JSONEventsClient_CPPFLAGS = $(STD_DEFINES_AND_INCLUDES)
JSONEventsClient_LDADD = $(NODEMANAGER_LA)

EXTRA_DIST = \
	OpenBTS.example.sql \
	setUpFiles.sh \
	openbts.conf \
	generateConfigTable.sh \
	exportConfigTable.sh \
	importConfigTable.sh

install: OpenBTS OpenBTSCLI
	mkdir -p "$(DESTDIR)/OpenBTS/"
	install OpenBTS "$(DESTDIR)/OpenBTS/"
	install OpenBTSCLI "$(DESTDIR)/OpenBTS/"
	install .gdbinit "$(DESTDIR)/OpenBTS/"
	mkdir -p "$(DESTDIR)/etc/init/"
	install openbts.conf "$(DESTDIR)/etc/init/"
	mkdir -p "$(DESTDIR)/etc/OpenBTS/"
	install iptables.rules "$(DESTDIR)/etc/OpenBTS/"
	install OpenBTS.example.sql "$(DESTDIR)/etc/OpenBTS/"
	mkdir -p "$(DESTDIR)/etc/rsyslog.d/"
	mkdir -p "$(DESTDIR)/etc/logrotate.d/"
	install rsyslogd.OpenBTS.conf "$(DESTDIR)/etc/rsyslog.d/OpenBTS.conf"
	install OpenBTS.logrotate "$(DESTDIR)/etc/logrotate.d/OpenBTS"
	mkdir -p "$(DESTDIR)/home/openbts/"
	install openbtsconfig "$(DESTDIR)/home/openbts/"
