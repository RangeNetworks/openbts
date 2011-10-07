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

AM_CPPFLAGS = $(STD_DEFINES_AND_INCLUDES) $(USB_INCLUDES) $(WITH_INCLUDES)
AM_CXXFLAGS = -DOMNITHREAD_POSIX=1 -lusb-1.0 -ldl -lpthread

EXTRA_DIST = \
	README \
	README.Talgorithm

noinst_LTLIBRARIES = libtransceiver.la

libtransceiver_la_SOURCES = \
	rnrad1Core.cpp                   \
        fusb.cpp                         \
        rnrad1Rx.cpp                     \
        rnrad1Tx.cpp \
	radioInterface.cpp \
	sigProcLib.cpp \
	Transceiver.cpp \
	RAD1Device.cpp \
	DummyLoad.cpp


noinst_PROGRAMS = \
	RAD1ping \
	transceiver \
	sigProcLibTest \
	RAD1Cmd \
	RAD1SN \
	RAD1RxRawPower

noinst_HEADERS = \
        ad9862.h                        \
	bytesex.h			\
	commands.h			\
        fusb.h                          \
        fpga_regs.h                     \
        ids.h                           \
        i2c.h                           \
        interfaces.h                    \
        spi.h				\
        rnrad1Core.h			\
	rnrad1.h			\
	Complex.h \
	radioInterface.h \
	radioDevice.h \
	sigProcLib.h \
	Transceiver.h \
	RAD1Device.h \
	DummyLoad.h

RAD1ping_SOURCES = RAD1ping.cpp
RAD1ping_LDADD = \
	libtransceiver.la \
	$(COMMON_LA) $(SQLITE_LA) 

RAD1RxRawPower_SOURCES = RAD1RxRawPower.cpp
RAD1RxRawPower_LDADD = \
        libtransceiver.la \
        $(COMMON_LA) $(SQLITE_LA)

RAD1Cmd_SOURCES = RAD1Cmd.cpp
RAD1Cmd_LDADD = \
        libtransceiver.la \
        $(COMMON_LA) $(SQLITE_LA)

RAD1SN_SOURCES = RAD1SN.cpp
RAD1SN_LDADD = \
        libtransceiver.la \
        $(COMMON_LA) $(SQLITE_LA)


transceiver_SOURCES = runTransceiver.cpp
transceiver_LDADD = \
	libtransceiver.la \
	$(GSM_LA) \
	$(COMMON_LA) $(SQLITE_LA) 

sigProcLibTest_SOURCES = sigProcLibTest.cpp
sigProcLibTest_LDADD = \
	libtransceiver.la \
	$(GSM_LA) \
	$(COMMON_LA) $(SQLITE_LA)



MOSTLYCLEANFILES +=

#radioInterface.cpp
#ComplexTest.cpp
#sigProcLibTest.cpp
#sweepGenerator.cpp
#testRadio.cpp

