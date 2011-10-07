/*
* Copyright 2009 Free Software Foundation, Inc.
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

#include "F16.h"


#include <iostream>

using namespace std;

int main(int argc, char **argv)
{

	F16 a = 2.5;
	F16 b = 1.5;
	F16 c = 2.5 * 1.5;
	F16 d = c + a;
	F16 e = 10;
	cout << a << ' ' << b << ' ' << c << ' ' << d << ' ' << e << endl;

	a *= 3;
	b *= 0.3;
	c *= e;
	cout << a << ' ' << b << ' ' << c << ' ' << d << endl;

	a /= 3;
	b /= 0.3;
	c = d * 0.05;
	cout << a << ' ' << b << ' ' << c << ' ' << d << endl;

	F16 f = a/d;
	cout << f << ' ' << f+0.5 << endl;
}
