/*
* Copyright 2011 Free Software Foundation, Inc.
*
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

#include <URLEncode.h>
#include <string>
#include <string.h>
#include <ctype.h>

using namespace std;

//based on javascript encodeURIComponent()
string URLEncode(const string &c)
{
	static const char *digits = "01234567890ABCDEF";
	string retVal="";
	for (int i=0; i<c.length(); i++)
	{
		const char ch = c[i];
		if (isalnum(ch) || strchr("-_.!~'()",ch)) {
			retVal += ch;
		} else {
			retVal += '%';
			retVal += digits[(ch>>4) & 0x0f];
			retVal += digits[ch & 0x0f];
		}
	}
	return retVal;
}

