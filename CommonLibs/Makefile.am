#
# Copyright 2008, 2009 Free Software Foundation, Inc.
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
AM_CXXFLAGS = -Wall -ldl -O3 -g -lpthread

noinst_LTLIBRARIES = libcommon.la

libcommon_la_SOURCES = \
	BitVector.cpp \
	LinkedLists.cpp \
	Sockets.cpp \
	Threads.cpp \
	Timeval.cpp \
	Logger.cpp \
	URLEncode.cpp \
	Configuration.cpp

noinst_PROGRAMS = \
	BitVectorTest \
	InterthreadTest \
	SocketsTest \
	TimevalTest \
	RegexpTest \
	VectorTest \
	ConfigurationTest \
	LogTest \
	F16Test

noinst_HEADERS = \
	BitVector.h \
	Interthread.h \
	LinkedLists.h \
	Sockets.h \
	Threads.h \
	Timeval.h \
	Regexp.h \
	Vector.h \
	URLEncode.h \
	Configuration.h \
	F16.h \
	Logger.h

BitVectorTest_SOURCES = BitVectorTest.cpp
BitVectorTest_LDADD = libcommon.la

InterthreadTest_SOURCES = InterthreadTest.cpp
InterthreadTest_LDADD = libcommon.la
InterthreadTest_LDFLAGS = -lpthread

SocketsTest_SOURCES = SocketsTest.cpp
SocketsTest_LDADD = libcommon.la
SocketsTest_LDFLAGS = -lpthread

TimevalTest_SOURCES = TimevalTest.cpp
TimevalTest_LDADD = libcommon.la

VectorTest_SOURCES = VectorTest.cpp
VectorTest_LDADD = libcommon.la

RegexpTest_SOURCES = RegexpTest.cpp
RegexpTest_LDADD = libcommon.la

ConfigurationTest_SOURCES = ConfigurationTest.cpp
ConfigurationTest_LDADD = libcommon.la 	$(SQLITE_LA)

LogTest_SOURCES = LogTest.cpp
LogTest_LDADD = libcommon.la $(SQLITE_LA)

F16Test_SOURCES = F16Test.cpp

MOSTLYCLEANFILES += testSource testDestination


