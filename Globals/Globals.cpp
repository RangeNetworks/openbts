/**@file Global system parameters. */
/*
* Copyright 2008, 2009, 2010, 2011 Free Software Foundation, Inc.
* Copyright 2010 Kestrel Signal Processing, Inc.
* Copyright 2011 Range Networks, Inc.
*
* This software is distributed under the terms of the GNU Affero Public License.
* See the COPYING file in the main directory for details.
*
* This use of this software may be subject to additional restrictions.
* See the LEGAL file in the main directory for details.

	This program is free software: you can redistribute it and/or modify
	it under the terms of the GNU Affero General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU Affero General Public License for more details.

	You should have received a copy of the GNU Affero General Public License
	along with this program.  If not, see <http://www.gnu.org/licenses/>.

*/

#include "config.h"
#include <Globals.h>
#include <CLI.h>
#include <TMSITable.h>
#include <URLEncode.h>

#define PROD_CAT "P"

const char *gVersionString = "release " VERSION " " PROD_CAT " built " __DATE__ " rev" SVN_REV " ";

const char* gOpenBTSWelcome =
	//23456789123456789223456789323456789423456789523456789623456789723456789
	"OpenBTS\n"
	"Copyright 2008, 2009, 2010, 2011 Free Software Foundation, Inc.\n"
	"Copyright 2010 Kestrel Signal Processing, Inc.\n"
	"Copyright 2011, 2012 Range Networks, Inc.\n"
        "Release " VERSION " " PROD_CAT " formal build date " __DATE__ " rev" SVN_REV "\n"
	"\"OpenBTS\" is a registered trademark of Range Networks, Inc.\n"
	"\nContributors:\n"
	"  Range Networks, Inc.:\n"
	"    David Burgess, Harvind Samra, Donald Kirker, Doug Brown,\n"
	"    Pat Thompson, Kurtis Heimerl\n"
	"  Kestrel Signal Processing, Inc.:\n"
	"    David Burgess, Harvind Samra, Raffi Sevlian, Roshan Baliga\n"
	"  GNU Radio:\n"
	"    Johnathan Corgan\n"
	"  Others:\n"
	"    Anne Kwong, Jacob Appelbaum, Joshua Lackey, Alon Levy\n"
	"    Alexander Chemeris, Alberto Escudero-Pascual, Thomas Tsou\n"
	"Incorporated L/GPL libraries and components:\n"
	"  libosip2, LGPL, 2.1 Copyright 2001-2007 Aymeric MOIZARD jack@atosc.org\n"
	"  libortp, LGPL, 2.1 Copyright 2001 Simon MORLAT simon.morlat@linphone.org\n"
	"  libusb, LGPL 2.1, various copyright holders, www.libusb.org\n"
#ifdef HAVE_LIBREADLINE
	", libreadline, GPLv3, www.gnu.org/software/readline/"
#endif
	"Incorporated BSD/MIT libraries and components:\n"
	"Incorporated public domain libraries and components:\n"
	"  sqlite3, released to public domain 15 Sept 2001, www.sqlite.org\n"
	"\n"
	"\nThis program comes with ABSOLUTELY NO WARRANTY.\n"
	"\nUse of this software may be subject to other legal restrictions,\n"
	"including patent licsensing and radio spectrum licensing.\n"
	"All users of this software are expected to comply with applicable\n"
	"regulations and laws.  See the LEGAL file in the source code for\n"
	"more information."
;


CommandLine::Parser gParser;

