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

AM_CPPFLAGS = $(STD_DEFINES_AND_INCLUDES) $(USB_INCLUDES) $(WITH_INCLUDES) $(OPENBTS_CPPFLAGS)
AM_CXXFLAGS = -Wall -pthread -ldl $(OPENBTS_CXXFLAGS)

DESTDIR = 

noinst_PROGRAMS = \
	OpenBTS \
	OpenBTSDo \
	OpenBTSCLI

OpenBTS_SOURCES = OpenBTS.cpp
OpenBTS_LDADD = \
	$(CLI_LA) \
	$(SIP_LA) \
	$(GSM_LA) \
	$(TRX_LA) \
	$(GLOBALS_LA) \
	$(CONTROL_LA) \
	$(SR_LA) \
	$(COMMON_LA) \
	$(SQLITE_LA) \
	$(SMS_LA) \
	$(OSIP_LIBS) \
	$(ORTP_LIBS)

OpenBTSCLI_SOURCES = OpenBTSCLI.cpp
OpenBTSDo_SOURCES = OpenBTSDo.cpp

EXTRA_DIST = \
	OpenBTS.example.sql

install: OpenBTS OpenBTSCLI OpenBTSDo
	mkdir -p "$(DESTDIR)/OpenBTS/"
	install OpenBTS "$(DESTDIR)/OpenBTS/"
	install OpenBTSCLI "$(DESTDIR)/OpenBTS/"
	install OpenBTSDo "$(DESTDIR)/OpenBTS/"
	mkdir -p "$(DESTDIR)/etc/OpenBTS/"
	install OpenBTS.example.sql "$(DESTDIR)/etc/OpenBTS/"
