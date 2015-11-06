#
# Copyright 2008 Free Software Foundation, Inc.
# Copyright 2010 Range Networks, Inc.
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

DESTDIR = 

AM_CFLAGS = $(STD_DEFINES_AND_INCLUDES) -std=gnu99 -march=native
# AM_CPPFLAGS = $(STD_DEFINES_AND_INCLUDES)
# AM_CXXFLAGS = -ldl -lpthread

#UHD wins if both are defined
if UHD
AM_CPPFLAGS += $(UHD_CFLAGS)
else
if USRP1
AM_CPPFLAGS += $(USRP_CFLAGS)
endif
endif

rev2dir = $(datadir)/usrp/rev2
rev4dir = $(datadir)/usrp/rev4

dist_rev2_DATA = std_inband.rbf
dist_rev4_DATA = std_inband.rbf

EXTRA_DIST = \
	README \
	README.Talgorithm

noinst_LTLIBRARIES = libtransceiver.la

COMMON_SOURCES = \
	radioInterface.cpp \
	radioVector.cpp \
	radioClock.cpp \
	sigProcLib.cpp \
	Transceiver.cpp \
	DummyLoad.cpp \
	convolve.c \
	convert.c

libtransceiver_la_SOURCES = \
	$(COMMON_SOURCES) \
	Resampler.cpp \
	radioInterfaceResamp.cpp

noinst_PROGRAMS = \
	transceiver

noinst_HEADERS = \
	Complex.h \
	radioInterface.h \
	radioVector.h \
	radioClock.h \
	radioDevice.h \
	sigProcLib.h \
	Transceiver.h \
	USRPDevice.h \
	DummyLoad.h \
	Resampler.h \
	convolve.h \
	convert.h

transceiver_SOURCES = runTransceiver.cpp
transceiver_LDADD = \
	libtransceiver.la \
	$(GSM_LA) \
	$(COMMON_LA) $(SQLITE_LA)

#uhd wins
if UHD
libtransceiver_la_SOURCES += UHDDevice.cpp
transceiver_LDADD += $(UHD_LIBS)
else
if USRP1
libtransceiver_la_SOURCES += USRPDevice.cpp
transceiver_LDADD += $(USRP_LIBS)
else
#we should never be here, as one of the above mustbe defined for us to build
endif
endif

#uhd wins
if UHD
install: transceiver
	@mkdir -p "$(DESTDIR)/OpenBTS/"
	install transceiver "$(DESTDIR)/OpenBTS/"
else
if USRP1
install: transceiver
	@mkdir -p "$(DESTDIR)/usr/local/share/usrp/rev4/"
	install std_inband.rbf "$(DESTDIR)/usr/local/share/usrp/rev4/"
	@mkdir -p "$(DESTDIR)/OpenBTS/"
	install transceiver "$(DESTDIR)/OpenBTS/"
else
#we should never be here, as one of the above mustbe defined for us to build
endif
endif
