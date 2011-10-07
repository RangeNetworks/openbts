/*
* Copyright 2008 Free Software Foundation, Inc.
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


#ifndef REGEXPW_H
#define REGEXPW_H

#include <regex.h>
#include <iostream>
#include <stdlib.h>



class Regexp {

	private:

	regex_t mRegex;


	public:

	Regexp(const char* regexp, int flags=REG_EXTENDED)
	{
		int result = regcomp(&mRegex, regexp, flags);
		if (result) {
			char msg[256];
			regerror(result,&mRegex,msg,255);
			std::cerr << "Regexp compilation of " << regexp << " failed: " << msg << std::endl;
			abort();
		}
	}

	~Regexp()
		{ regfree(&mRegex); }

	bool match(const char *text, int flags=0) const
		{ return regexec(&mRegex, text, 0, NULL, flags)==0; }

};


#endif
