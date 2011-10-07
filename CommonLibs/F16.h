/*
* Copyright 2009 Free Software Foundation, Inc.
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


#ifndef F16_H
#define F16_H

#include <stdint.h>
#include <ostream>



/** Round a float to the appropriate F16 value. */
inline int32_t _f16_round(float f)
{
	if (f>0.0F) return (int32_t)(f+0.5F);
	if (f<0.0F) return (int32_t)(f-0.5F);
	return 0;
}



/** A class for F15.16 fixed point arithmetic with saturation.  */
class F16 {


	private:

	int32_t mV;


	public:

	F16() {}

	F16(int i) { mV = i<<16; }
	F16(float f) { mV = _f16_round(f*65536.0F); }
	F16(double f) { mV = _f16_round((float)f*65536.0F); }

	int32_t& raw() { return mV; }
	const int32_t& raw() const { return mV; }

	float f() const { return mV/65536.0F; }

	//operator float() const { return mV/65536.0F; }
	//operator int() const { return mV>>16; }

	F16 operator=(float f)
	{
		mV = _f16_round(f*65536.0F);
		return *this;
	}

	F16 operator=(int i)
	{
		mV = i<<16;
		return *this;
	}

	F16 operator=(const F16& other)
	{
		mV = other.mV;
		return mV;
	}

	F16 operator+(const F16& other) const
	{
		F16 retVal;
		retVal.mV = mV + other.mV;
		return retVal;
	}

	F16& operator+=(const F16& other)
	{
		mV += other.mV;
		return *this;
	}

	F16 operator-(const F16& other) const
	{
		F16 retVal;
		retVal.mV = mV - other.mV;
		return retVal;
	}

	F16& operator-=(const F16& other)
	{
		mV -= other.mV;
		return *this;
	}

	F16 operator*(const F16& other) const
	{
		F16 retVal;
		int64_t p = (int64_t)mV * (int64_t)other.mV;
		retVal.mV = p>>16;
		return retVal;
	}

	F16& operator*=(const F16& other)
	{
		int64_t p = (int64_t)mV * (int64_t)other.mV;
		mV = p>>16;
		return *this;
	}

	F16 operator*(float f) const
	{
		F16 retVal;
		retVal.mV = mV * f;
		return retVal;
	}

	F16& operator*=(float f)
	{
		mV *= f;
		return *this;
	}

	F16 operator/(const F16& other) const
	{
		F16 retVal;
		int64_t pV = (int64_t)mV << 16;
		retVal.mV = pV / other.mV;
		return retVal;
	}

	F16& operator/=(const F16& other)
	{
		int64_t pV = (int64_t)mV << 16;
		mV = pV / other.mV;
		return *this;
	}

	F16 operator/(float f) const
	{
		F16 retVal;
		retVal.mV = mV / f;
		return retVal;
	}

	F16& operator/=(float f)
	{
		mV /= f;
		return *this;
	}

	bool operator>(const F16& other) const
	{
		return mV>other.mV;
	}

	bool operator<(const F16& other) const
	{
		return mV<other.mV;
	}

	bool operator==(const F16& other) const
	{
		return mV==other.mV;
	}

	bool operator>(float f) const
	{
		return (mV/65536.0F) > f;
	}

	bool operator<(float f) const
	{
		return (mV/65536.0F) < f;
	}

	bool operator==(float f) const
	{
		return (mV/65536.0F) == f;
	}

};



inline std::ostream& operator<<(std::ostream& os, const F16& v)
{
	os << v.f();
	return os;
}

#endif

