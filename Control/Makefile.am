#
# Copyright 2008 Free Software Foundation, Inc.
# Copyright 2010 Kestrel Signal Processing, Inc.
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

AM_CPPFLAGS = $(STD_DEFINES_AND_INCLUDES)
AM_CXXFLAGS = -Wall

EXTRA_DIST = README.Control

noinst_LTLIBRARIES = libcontrol.la

libcontrol_la_SOURCES = \
	TransactionTable.cpp \
	TMSITable.cpp \
	CallControl.cpp \
	SMSControl.cpp \
	ControlCommon.cpp \
	MobilityManagement.cpp \
	RadioResource.cpp \
	DCCHDispatch.cpp \
	RRLPServer.cpp


noinst_HEADERS = \
	ControlCommon.h \
	SMSControl.h \
	TransactionTable.h \
	TMSITable.h \
	RadioResource.h \
	MobilityManagement.h \
	CallControl.h \
	TMSITable.h \
	RRLPServer.h
