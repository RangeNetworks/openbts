#
# Copyright 2008 Free Software Foundation, Inc.
# Copyright 2010 Kestrel Signal Processing, Inc.
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

# AM_CPPFLAGS = $(STD_DEFINES_AND_INCLUDES)
# AM_CXXFLAGS = -Wall

EXTRA_DIST = README.Control

noinst_LTLIBRARIES = libcontrol.la


# L3CallControl.cpp
libcontrol_la_SOURCES = \
	L3TermCause.cpp \
	L3CallControl.cpp \
	PagingEntry.cpp \
	L3Handover.cpp \
	L3SMSControl.cpp \
	L3MobilityManagement.cpp \
	L3StateMachine.cpp \
	L3SupServ.cpp \
	L3MMLayer.cpp \
	L3TranEntry.cpp \
	L3LogicalChannel.cpp \
	L3Utils.cpp \
	TMSITable.cpp \
	ControlTransfer.cpp \
	DCCHDispatch.cpp \
	CBS.cpp \
	RRLPServer.cpp


# TODO - move CollectMSInfo.cpp and RRLPQueryController.cpp to RRLP directory.

noinst_HEADERS = \
	L3TermCause.h \
	CodecSet.h \
	PagingEntry.h \
	L3Handover.h \
	L3SMSControl.h \
	L3CallControl.h \
	L3MobilityManagement.h \
	L3StateMachine.h \
	L3SupServ.h \
	L3MMLayer.h \
	L3TranEntry.h \
	L3LogicalChannel.h \
	L3Utils.h \
	ControlCommon.h \
	ControlTransfer.h \
	TMSITable.h \
	TMSITable.h \
	CBS.h \
	RRLPServer.h
