/**@file Global system parameters. */
/*
* Copyright 2008, 2009, 2010 Free Software Foundation, Inc.
* Copyright 2010 Kestrel Signal Processing, Inc.
* Copyright 2011-2021 Range Networks, Inc.
*
* This software is distributed under multiple licenses;
* see the COPYING file in the main directory for licensing
* information for this specific distribution.
*
* This use of this software may be subject to additional restrictions.
* See the LEGAL file in the main directory for details.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

*/

#include "config.h"
#include <Globals.h>

#define PROD_CAT "P"

#define FEATURES "+GPRS "

// Note that __TIME__ will only get updated if this file gets recompiled, so
// it will usually be wrong.  Correct solution is to put all the special defines
// in a header file that always gets generated and recompiled.
const char *gVersionString = "release " VERSION "+" REPO_REV FEATURES PROD_CAT " built " TIMESTAMP_ISO " ";

const char* gOpenBTSWelcome =
	//23456789123456789223456789323456789423456789523456789623456789723456789
	"OpenBTS\n"
	"Copyright 2008, 2009, 2010 Free Software Foundation, Inc.\n"
	"Copyright 2010 Kestrel Signal Processing, Inc.\n"
	"Copyright 2011-2021 Range Networks, Inc.\n"
	"Release " VERSION "+" REPO_REV " " PROD_CAT " formal build date " TIMESTAMP_ISO "\n"
	"\"OpenBTS\" is a registered trademark of Range Networks, Inc.\n"
	"\nContributors:\n"
	"  Range Networks, Inc.:\n"
	"    David Burgess, Harvind Samra, Donald Kirker, Doug Brown,\n"
	"    Pat Thompson, Kurtis Heimerl, Michael Iedema, Dave Gotwisner\n"
	"  Kestrel Signal Processing, Inc.:\n"
	"    David Burgess, Harvind Samra, Raffi Sevlian, Roshan Baliga\n"
	"  GNU Radio:\n"
	"    Johnathan Corgan\n"
	"  Others:\n"
	"    Anne Kwong, Jacob Appelbaum, Joshua Lackey, Alon Levy\n"
	"    Alexander Chemeris, Alberto Escudero-Pascual\n"
	"Incorporated L/GPL libraries and components:\n"
	"  libortp, LGPL, 2.1 Copyright 2001 Simon MORLAT simon.morlat@linphone.org\n"
	"  libusb, LGPL 2.1, various copyright holders, www.libusb.org\n"
        "  libzmq, LGPL 3:\n"
        "      Copyright (c) 2009-2011 250bpm s.r.o.\n"
        "      Copyright (c) 2011 Botond Ballo\n"
        "      Copyright (c) 2007-2009 iMatix Corporation\n"

	"Incorporated BSD/MIT-style libraries and components:\n"
	"  A5/1 Pedagogical Implementation, Simplified BSD License, Copyright 1998-1999 Marc Briceno, Ian Goldberg, and David Wagner\n"
	"  JsonBox, Copyright 2011 Anhero Inc.\n"
        "  Google Core Dumper, BSD 3-Clause License, Copyright (c) 2005-2007, Google Inc.\n"
	"Incorporated public domain libraries and components:\n"
	"  sqlite3, released to public domain 15 Sept 2001, www.sqlite.org\n"
	"\n"
	"\nThis program comes with ABSOLUTELY NO WARRANTY.\n"
	"\nUse of this software may be subject to other legal restrictions,\n"
	"including patent licensing and radio spectrum licensing.\n"
	"All users of this software are expected to comply with applicable\n"
	"regulations and laws.  See the LEGAL file in the source code for\n"
	"more information.\n"
	"\n"
	"Note to US Government Users:\n"
	" The OpenBTS software applications and associated documentation are \"Commercial\n"
	" Item(s),\" as that term is defined at 48 C.F.R. Section 2.101, consisting of\n"
	" \"Commercial Computer Software\" and \"Commercial Computer Software Documentation,\"\n"
	" as such terms are used in 48 C.F.R. 12.212 or 48 C.F.R. 227.7202, as\n"
	" applicable. Consistent with 48 C.F.R. 12.212 or 48 C.F.R. Sections 227.7202-1\n"
	" through 227.7202-4, as applicable, the Commercial Computer Software and\n"
	" Commercial Computer Software Documentation are being licensed to U.S. Government\n"
	" end users (a) only as Commercial Items and (b) with only those rights as are\n"
	" granted to all other end users pursuant to the terms and conditions of\n"
	" Range Networks' software licenses and master customer agreement.\n"


;
