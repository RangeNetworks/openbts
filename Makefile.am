#
# Copyright 2008 Free Software Foundation, Inc.
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

ACLOCAL_AMFLAGS = -I config
AM_CPPFLAGS = $(STD_DEFINES_AND_INCLUDES) $(USB_INCLUDES) $(WITH_INCLUDES)
AM_CXXFLAGS = -Wall -pthread -ldl
#AM_CXXFLAGS = -Wall -O2 -NDEBUG -pthread -ldl
#AM_CFLAGS = -Wall -O2 -NDEBUG -pthread -ldl

# Order must be preserved
_SUBDIRS = \
	config \
	sqlite3 \
	CommonLibs \
	Globals \
	SR \
	CLI \
	SIP \
	GSM \
	SMS \
	TRXManager \
	Control \
	Peering \
	GPRS \
	SGSNGGSN \
	apps \
	doc


#if UHD or USRP1 defined, build Transceiver52M as well
if UHD
SUBDIRS = $(_SUBDIRS) \
	  Transceiver52M
UHD = 1
else
if USRP1
SUBDIRS = $(_SUBDIRS) \
	  Transceiver52M
USRP1 = 1
else
SUBDIRS = $(_SUBDIRS) \
	  TransceiverRAD1
endif
endif

EXTRA_DIST = \
	INSTALLATION \
	LEGAL \
	COPYING \
	README

install: all
	$(MAKE) -C ./apps install
if UHD
	$(MAKE) -C ./Transceiver52M install
else
if USRP1
	$(MAKE) -C ./Transceiver52M install
else
	$(MAKE) -C ./TransceiverRAD1 install
endif
endif

dox: FORCE
	doxygen doxconfig

FORCE:
