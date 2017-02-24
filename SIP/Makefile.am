#
# Copyright 2008 Free Software Foundation, Inc.
# Copyright 2014 Range Networks, Inc.
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

# Previously: -I$(ORTP_INCLUDEDIR) $(ORTP_CPPFLAGS)
# AM_CPPFLAGS = $(STD_DEFINES_AND_INCLUDES)
# AM_CXXFLAGS = -Wall -Wextra

noinst_LTLIBRARIES = libSIP.la

libSIP_la_CXXFLAGS = $(AM_CXXFLAGS) -Wextra
libSIP_la_SOURCES = \
	SIPRtp.cpp \
	SIPParse.cpp \
	SIPMessage.cpp \
	SIPDialog.cpp \
	SIP2Interface.cpp \
	SIPBase.cpp \
	SIPUtility.cpp \
	SIPTransaction.cpp

noinst_HEADERS = \
	SIPRtp.h \
	SIPParse.h \
	SIPBase.h \
	SIPDialog.h \
	SIPTransaction.h \
	SIP2Interface.h \
	SIPMessage.h \
	SIPUtility.h \
	SIPExport.h
