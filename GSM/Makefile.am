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

AM_CPPFLAGS = $(STD_DEFINES_AND_INCLUDES)
#AM_CXXFLAGS = -O2 -g

noinst_LTLIBRARIES = libGSM.la

libGSM_la_SOURCES = \
	GSM610Tables.cpp \
	GSMCommon.cpp \
	GSMConfig.cpp \
	GSML1FEC.cpp \
	GSML2LAPDm.cpp \
	GSML3CCElements.cpp \
	GSML3CCMessages.cpp \
	GSML3CommonElements.cpp \
	GSML3Message.cpp \
	GSML3MMElements.cpp \
	GSML3MMMessages.cpp \
	GSML3RRElements.cpp \
	GSML3RRMessages.cpp \
	GSMLogicalChannel.cpp \
	GSMSAPMux.cpp \
	GSMTDMA.cpp \
	GSMTransfer.cpp \
	GSMTAPDump.cpp \
	PowerManager.cpp\
	PhysicalStatus.cpp

noinst_HEADERS = \
 	GSM610Tables.h \
	GSMCommon.h \
	GSMConfig.h \
	GSML1FEC.h \
	GSML2LAPDm.h \
	GSML3CCElements.h \
	GSML3CCMessages.h \
	GSML3CommonElements.h \
	GSML3Message.h \
	GSML3MMElements.h \
	GSML3MMMessages.h \
	GSML3RRElements.h \
	GSML3RRMessages.h \
	GSMLogicalChannel.h \
	GSMSAPMux.h \
	GSMTDMA.h \
	GSMTransfer.h \
	PowerManager.h \
	GSMTAPDump.h \
	gsmtap.h \
	PhysicalStatus.h

