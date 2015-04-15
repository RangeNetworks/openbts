dnl
dnl Copyright 2008, 2009, 2010 Free Software Foundation, Inc.
dnl Copyright 2014 Range Networks, Inc.
dnl
dnl This software is distributed under the terms of the GNU Public License.
dnl See the COPYING file in the main directory for details.
dnl
dnl This program is free software: you can redistribute it and/or modify
dnl it under the terms of the GNU General Public License as published by
dnl the Free Software Foundation, either version 3 of the License, or
dnl (at your option) any later version.
dnl
dnl This program is distributed in the hope that it will be useful,
dnl but WITHOUT ANY WARRANTY; without even the implied warranty of
dnl MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
dnl GNU General Public License for more details.
dnl
dnl You should have received a copy of the GNU General Public License
dnl along with this program.  If not, see <http://www.gnu.org/licenses/>.
dnl

AC_PREREQ([2.57])
# must be AC_INIT([openbts], [5.0-master])
AC_INIT(openbts,5.0-master) 

AC_CANONICAL_BUILD
AC_CANONICAL_HOST
AC_CANONICAL_TARGET

AC_CONFIG_AUX_DIR([.])
AM_INIT_AUTOMAKE([-Wno-portability])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_MACRO_DIR([config])
AC_CONFIG_SRCDIR([apps/OpenBTS.cpp]) # was AC_CONFIG_SRCDIR([config/Makefile.am])

dnl Checks for programs.
: ${CXXFLAGS=""} ## remove auto setting of CXXFLAGS = -g -O2
AC_PROG_CXX 
AC_PROG_AWK
AC_PROG_CC
AC_PROG_INSTALL
AC_PROG_LN_S
AC_PROG_MAKE_SET
AM_PROG_AS ## GNU radio
AC_PATH_PROG([RM_PROG], [rm])  ## GNU radio
# (oley) uncomment if any libraries are being generated
# AC_PROG_RANLIB	

# AC_LIBTOOL_WIN32_DLL
# AC_PROG_LIBTOOL
LT_INIT

AC_ENABLE_SHARED	dnl do build shared libraries
AC_DISABLE_STATIC	dnl don't build static libraries

dnl Checks for header files.
AC_HEADER_STDC
dnl This is required for GnuRadio includes to understand endianess correctly:
AC_CHECK_HEADERS([byteswap.h])
# AC_CHECK_HEADERS([arpa/inet.h fcntl.h inttypes.h limits.h malloc.h netdb.h netinet/in.h stddef.h stdint.h stdlib.h string.h sys/file.h sys/ioctl.h sys/mount.h sys/param.h sys/socket.h sys/time.h syslog.h unistd.h utime.h])

dnl Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST
AC_C_INLINE
AC_TYPE_SIZE_T
AC_HEADER_TIME
AC_C_BIGENDIAN

AC_ARG_WITH(usrp1, [
    AS_HELP_STRING([--with-usrp1],
        [enable USRP1 gnuradio based transceiver])
])

AC_ARG_WITH(uhd, [
    AS_HELP_STRING([--with-uhd],
        [enable UHD based transceiver])
])

AC_ARG_WITH(singledb, [
    AS_HELP_STRING([--with-singledb],
        [enable single daughterboard use on USRP1])
])

AS_IF([test "x$with_usrp1" = "xyes"], [
    PKG_CHECK_MODULES(USRP, usrp >= 3.3)
    # Find and define supported SIMD extensions
    AX_EXT
])

AS_IF([test "x$with_uhd" = "xyes"],[
    PKG_CHECK_MODULES(UHD, uhd >= 003.009,
        [AC_DEFINE(USE_UHD_3_9, 1, UHD version 3.9.0 or higher)],
        [PKG_CHECK_MODULES(UHD, uhd >= 003.005.004)]
    )
    AC_DEFINE(USE_UHD, 1, Other UHD versions)
    AX_EXT
])

AS_IF([test "x$with_singledb" = "xyes"], [
    AC_DEFINE(SINGLEDB, 1, Define to 1 for single daughterboard)
])

AM_CONDITIONAL(UHD, [test "x$with_uhd" = "xyes"])
AM_CONDITIONAL(USRP1, [test "x$with_usrp1" = "xyes"])

dnl Check for libzmq
if test ! -r "/usr/include/zmq.h" -a ! -r "/usr/local/include/zmq.h"; then
    AC_MSG_ERROR([/usr/local/include/zmq.h not found. Install the libzmq3-dev package manually or run $ sudo ./NodeManager/install_libzmq.sh])
fi
if test ! -r "/usr/include/zmq.hpp" -a ! -r "/usr/local/include/zmq.hpp"; then
    AC_MSG_ERROR([/usr/local/include/zmq.hpp not found. Install the libzmq3-dev package manually or run $ sudo ./NodeManager/install_libzmq.sh])
fi
AC_CHECK_LIB(zmq, zmq_init, ,[AC_MSG_ERROR([Cannot link with -lzmq. Install the libzmq3-dev package manually or run $ sudo ./NodeManager/install_libzmq.sh])])
AC_MSG_CHECKING([whether libzmq installation works])
AC_LINK_IFELSE([AC_LANG_PROGRAM([#include <zmq.h>],
                                [zmq_init(1);])],
               [AC_MSG_RESULT([yes])],
               [AC_MSG_ERROR([no. Install the libzmq3-dev package manually or run $ sudo ./NodeManager/install_libzmq.sh])])

dnl Check for liba53
if test ! -r "/usr/include/a53.h" -a ! -r "/usr/local/include/a53.h"; then
    AC_MSG_ERROR([/usr/include/a53.h not found. Install the liba53 package or manually build and install with $ sudo ./tools/install_liba53.sh])
fi
AC_CHECK_LIB(a53, _init, ,[AC_MSG_ERROR([Cannot link with -la53. Install the liba53 package or manually build and install with $ sudo ./tools/install_liba53.sh])])

dnl Check for sip+rtp libs
AC_MSG_CHECKING([ortp api version])
AS_IF([gcc -I=/usr/local/include -o config/test_ortp_version config/test_ortp_version.cpp -lortp &> /dev/null], [
	AC_DEFINE(ORTP_NEW_API, 1, rtp_session_set_local_addr takes four arguments)
	AC_MSG_RESULT([>= 0.22.0])
	],
	[AC_MSG_RESULT([< 0.22.0])])

dnl Check for other misc libs
# AC_CHECK_LIB([sqlite3], [main])

dnl Check for glibc-specific network functions
AC_CHECK_FUNC(gethostbyname_r, [AC_DEFINE(HAVE_GETHOSTBYNAME_R, 1, [Define if libc implements gethostbyname_r])])
AC_CHECK_FUNC(gethostbyname2_r, [AC_DEFINE(HAVE_GETHOSTBYNAME2_R, 1, [Define if libc implements gethostbyname2_r])])

dnl Output files
AC_CONFIG_FILES([\
    Makefile \
    apps/Makefile \
    config/Makefile \
    doc/Makefile \
    CommonLibs/Makefile \
    Globals/Makefile \
    Control/Makefile \
    GSMShare/Makefile \
    GSM/Makefile \
    GPRS/Makefile \
    SGSNGGSN/Makefile \
    SIP/Makefile \
    SMS/Makefile \
    TransceiverRAD1/Makefile \
    Transceiver52M/Makefile \
    TRXManager/Makefile \
    CLI/Makefile \
    Peering/Makefile \
    NodeManager/Makefile \
    Scanning/Makefile \
])
AC_OUTPUT
