/*
* Copyright 2008 Free Software Foundation, Inc.
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



#include "Timeval.h"

using namespace std;

void Timeval::future(unsigned offset)
{
	now();
	unsigned sec = offset/1000;
	unsigned msec = offset%1000;
	mTimeval.tv_usec += msec*1000;
	mTimeval.tv_sec += sec;
	if (mTimeval.tv_usec>1000000) {
		mTimeval.tv_usec -= 1000000;
		mTimeval.tv_sec += 1;
	}
}


struct timespec Timeval::timespec() const
{
	struct timespec retVal;
	retVal.tv_sec = mTimeval.tv_sec;
	retVal.tv_nsec = 1000 * (long)mTimeval.tv_usec;
	return retVal;
}


bool Timeval::passed() const
{
	Timeval nowTime;
	if (nowTime.mTimeval.tv_sec < mTimeval.tv_sec) return false;
	if (nowTime.mTimeval.tv_sec > mTimeval.tv_sec) return true;
	if (nowTime.mTimeval.tv_usec > mTimeval.tv_usec) return true;
	return false;
}

double Timeval::seconds() const
{
	return ((double)mTimeval.tv_sec) + 1e-6*((double)mTimeval.tv_usec);
}



long Timeval::delta(const Timeval& other) const
{
	// 2^31 milliseconds is just over 4 years.
	int32_t deltaS = other.sec() - sec();
	int32_t deltaUs = other.usec() - usec();
	return 1000*deltaS + deltaUs/1000;
}
	



ostream& operator<<(ostream& os, const Timeval& tv)
{
	os.setf( ios::fixed, ios::floatfield );
	os << tv.seconds();
	return os;
}


ostream& operator<<(ostream& os, const struct timespec& ts)
{
	os << ts.tv_sec << "," << ts.tv_nsec;
	return os;
}



// vim: ts=4 sw=4
