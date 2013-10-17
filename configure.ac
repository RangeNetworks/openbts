dnl
dnl Copyright 2008, 2009, 2010 Free Software Foundation, Inc.
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

AC_INIT(openbts-public,TRUNK)
AC_PREREQ(2.57)
AC_CONFIG_SRCDIR([config/Makefile.am])
AC_CONFIG_AUX_DIR([.])	
AC_CONFIG_MACRO_DIR([config])
AM_CONFIG_HEADER(config.h)

AC_CANONICAL_BUILD
AC_CANONICAL_HOST
AC_CANONICAL_TARGET

AM_INIT_AUTOMAKE

AM_PROG_AS
AC_PROG_CXX
AC_PROG_LN_S
AC_PROG_MAKE_SET
AC_PROG_INSTALL
AC_PATH_PROG([RM_PROG], [rm])

AC_LIBTOOL_WIN32_DLL
AC_ENABLE_SHARED	dnl do build shared libraries
AC_DISABLE_STATIC	dnl don't build static libraries
AC_PROG_LIBTOOL

dnl Checks for header files.
AC_HEADER_STDC
dnl This is required for GnuRadio includes to understand endianess correctly:
AC_CHECK_HEADERS([byteswap.h])

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
    PKG_CHECK_MODULES(UHD, uhd >= 003.004.000)
    AC_DEFINE(USE_UHD, 1, Define to 1 if using UHD)
    AX_EXT
])

AS_IF([test "x$with_singledb" = "xyes"], [
    AC_DEFINE(SINGLEDB, 1, Define to 1 for single daughterboard)
])

AM_CONDITIONAL(UHD, [test "x$with_uhd" = "xyes"])
AM_CONDITIONAL(USRP1, [test "x$with_usrp1" = "xyes"])

# Defines LIBUSB_TRANSFER_CANCELLED, LIBUSB_TRANSFER_COMPLETED, LIBUSB_SUCCESS, LIBUSB_ERROR_*
PKG_CHECK_MODULES(LIBUSB, libusb-1.0)

# Check for glibc-specific network functions
AC_CHECK_FUNC(gethostbyname_r, [AC_DEFINE(HAVE_GETHOSTBYNAME_R, 1, Define if libc implements gethostbyname_r)])
AC_CHECK_FUNC(gethostbyname2_r, [AC_DEFINE(HAVE_GETHOSTBYNAME2_R, 1, Define if libc implements gethostbyname2_r)])

dnl Output files
AC_CONFIG_FILES([\
    Makefile \
    apps/Makefile \
    config/Makefile \
    doc/Makefile \
    CommonLibs/Makefile \
    Globals/Makefile \
    Control/Makefile \
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
    SR/Makefile \
    sqlite3/Makefile \
])

AC_OUTPUT
