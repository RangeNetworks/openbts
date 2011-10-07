/*
* Copyright 2009, 2010 Free Software Foundation, Inc.
* Copyright 2010 Kestrel Signal Processing, Inc.
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



#include "Configuration.h"
#include <iostream>

using namespace std;

ConfigurationTable gConfig("exampleconfig.db");

void purgeConfig(void*,int,char const*, char const*, sqlite3_int64)
{
	//cout << "update hook" << endl;
	gConfig.purge();
}


int main(int argc, char *argv[])
{

	gConfig.setUpdateHook(purgeConfig);

	char *keys[5] = {"key1", "key2", "key3", "key4", "key5"};

	for (int i=0; i<5; i++) {
		gConfig.set(keys[i],i);
	}

	for (int i=0; i<5; i++) {
		cout << "table[" << keys[i] << "]=" << gConfig.getStr(keys[i]) <<  endl;
		cout << "table[" << keys[i] << "]=" << gConfig.getNum(keys[i]) <<  endl;
	}

	gConfig.unset("key1");
	for (int i=0; i<5; i++) {
		cout << "defined table[" << keys[i] << "]=" << gConfig.defines(keys[i]) <<  endl;
	}

	gConfig.set("key5","100 200 300 400");
	std::vector<unsigned> vect = gConfig.getVector("key5");
	cout << "vect length " << vect.size() << ": ";
	for (unsigned i=0; i<vect.size(); i++) cout << " " << vect[i];
	cout << endl;
}
