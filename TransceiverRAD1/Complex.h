/**@file templates for Complex classes
unlike the built-in complex<> templates, these inline most operations for speed
*/

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




#ifndef COMPLEXCPP_H
#define COMPLEXCPP_H

#include <math.h>
#include <ostream>


template<class Real> class Complex {

public:

  Real r, i;

  /**@name constructors */
  //@{
  /**@name from real */
  //@{
  Complex(Real real, Real imag) {r=real; i=imag;}	// x=complex(a,b)
  Complex(Real real) {r=real; i=0;}			// x=complex(a)
  //@}
  /**@name from nothing */
  //@{
  Complex() {r=(Real)0; i=(Real)0;}			// x=complex()
  //@}
  /**@name from other complex */
  //@{
  Complex(const Complex<float>& z) {r=z.r; i=z.i;}	// x=complex(z)
  Complex(const Complex<double>& z) {r=z.r; i=z.i;}	// x=complex(z)
  Complex(const Complex<long double>& z) {r=z.r; i=z.i;}	// x=complex(z)
  //@}
  //@}

  /**@name casting up from basic numeric types */
  //@{
  Complex& operator=(char a) { r=(Real)a; i=(Real)0; return *this; }
  Complex& operator=(int a) { r=(Real)a; i=(Real)0; return *this; }
  Complex& operator=(long int a) { r=(Real)a; i=(Real)0; return *this; }
  Complex& operator=(short a) { r=(Real)a; i=(Real)0; return *this; }
  Complex& operator=(float a) { r=(Real)a; i=(Real)0; return *this; }
  Complex& operator=(double a) { r=(Real)a; i=(Real)0; return *this; }
  Complex& operator=(long double a) { r=(Real)a; i=(Real)0; return *this; }
  //@}

  /**@name arithmetic */
  //@{
  /**@ binary operators */
  //@{
  Complex operator+(const Complex<Real>& a) const { return Complex<Real>(r+a.r, i+a.i); }
  Complex operator+(Real a) const { return Complex<Real>(r+a,i); }
  Complex operator-(const Complex<Real>& a) const { return Complex<Real>(r-a.r, i-a.i); }
  Complex operator-(Real a) const { return Complex<Real>(r-a,i); }
  Complex operator*(const Complex<Real>& a) const { return Complex<Real>(r*a.r-i*a.i, r*a.i+i*a.r); }
  Complex operator*(Real a) const { return Complex<Real>(r*a, i*a); }
  Complex operator/(const Complex<Real>& a) const { return operator*(a.inv()); }
  Complex operator/(Real a) const { return Complex<Real>(r/a, i/a); }
  //@}
  /*@name component-wise product */
  //@{
  Complex operator&(const Complex<Real>& a) const { return Complex<Real>(r*a.r, i*a.i); }
  //@}
  /*@name inplace operations */
  //@{
  Complex& operator+=(const Complex<Real>&);
  Complex& operator-=(const Complex<Real>&);
  Complex& operator*=(const Complex<Real>&);
  Complex& operator/=(const Complex<Real>&);
  Complex& operator+=(Real);
  Complex& operator-=(Real);
  Complex& operator*=(Real);
  Complex& operator/=(Real);
  //@}
  //@}

  /**@name comparisons */
  //@{
  bool operator==(const Complex<Real>& a) const { return ((i==a.i)&&(r==a.r)); }
  bool operator!=(const Complex<Real>& a) const { return ((i!=a.i)||(r!=a.r)); }
  bool operator<(const Complex<Real>& a) const { return norm2()<a.norm2(); }
  bool operator>(const Complex<Real>& a) const { return norm2()>a.norm2(); }
  //@}

  /// reciprocation
  Complex inv() const;	

  // unary functions -- inlined
  /**@name unary functions */
  //@{
  /**@name inlined */
  //@{
  Complex conj() const { return Complex<Real>(r,-i); }
  Real norm2() const { return i*i+r*r; }
  Complex flip() const { return Complex<Real>(i,r); }
  Real real() const { return r;}
  Real imag() const { return i;}
  Complex neg() const { return Complex<Real>(-r, -i); }
  bool isZero() const { return ((r==(Real)0) && (i==(Real)0)); }
  //@}
  /**@name not inlined due to outside calls */
  //@{
  Real abs() const { return ::sqrt(norm2()); }
  Real arg() const { return ::atan2(i,r); }
  float dB() const { return 10.0*log10(norm2()); }
  Complex exp() const { return expj(i)*(::exp(r)); }
  Complex unit() const; 			///< unit phasor with same angle
  Complex log() const { return Complex(::log(abs()),arg()); }
  Complex pow(double n) const { return expj(arg()*n)*(::pow(abs(),n)); }
  Complex sqrt() const { return pow(0.5); }
  //@}
  //@}

};


/**@name standard Complex manifestations */
//@{
typedef Complex<float> complex;
typedef Complex<double> dcomplex;
typedef Complex<short> complex16;
typedef Complex<long> complex32;
//@}


template<class Real> inline Complex<Real> Complex<Real>::inv() const
{
  Real nVal;

  nVal = norm2();
  return Complex<Real>(r/nVal, -i/nVal);
}

template<class Real> Complex<Real>& Complex<Real>::operator+=(const Complex<Real>& a)
{
  r += a.r;
  i += a.i;
  return *this;
}

template<class Real> Complex<Real>& Complex<Real>::operator*=(const Complex<Real>& a)
{
  operator*(a);
  return *this;
}

template<class Real> Complex<Real>& Complex<Real>::operator-=(const Complex<Real>& a)
{
  r -= a.r;
  i -= a.i;
  return *this;
}

template<class Real> Complex<Real>& Complex<Real>::operator/=(const Complex<Real>& a)
{
  operator/(a);
  return *this;
}


/* op= style operations with reals */

template<class Real> Complex<Real>& Complex<Real>::operator+=(Real a)
{
  r += a;
  return *this;
}

template<class Real> Complex<Real>& Complex<Real>::operator*=(Real a)
{
  r *=a;
  i *=a;
  return *this;
}

template<class Real> Complex<Real>& Complex<Real>::operator-=(Real a)
{
  r -= a;
  return *this;
}

template<class Real> Complex<Real>& Complex<Real>::operator/=(Real a)
{
  r /= a;
  i /= a;
  return *this;
}


template<class Real> Complex<Real> Complex<Real>::unit() const
{
  Real absVal = abs();
  return (Complex<Real>(r/absVal, i/absVal));
}



/**@name complex functions outside of the Complex<> class. */
//@{

/** this allows type-commutative multiplication */
template<class Real> Complex<Real> operator*(Real a, const Complex<Real>& z)
{
  return Complex<Real>(z.r*a, z.i*a);
}


/** this allows type-commutative addition */
template<class Real> Complex<Real> operator+(Real a, const Complex<Real>& z)
{
  return Complex<Real>(z.r+a, z.i);
}


/** this allows type-commutative subtraction */
template<class Real> Complex<Real> operator-(Real a, const Complex<Real>& z)
{
  return Complex<Real>(z.r-a, z.i);
}



/// e^jphi
template<class Real> Complex<Real> expj(Real phi)
{
  return Complex<Real>(cos(phi),sin(phi));
}

/// phasor expression of a complex number
template<class Real> Complex<Real> phasor(Real C, Real phi)
{
  return (expj(phi)*C);
}

/// formatted stream output
template<class Real> std::ostream& operator<<(std::ostream& os, const Complex<Real>& z)
{
  os << z.r << ' ';
  //os << z.r << ", ";
  //if (z.i>=0) { os << "+"; }
  os << z.i << "j";
  os << "\n";
  return os;
}

//@}


#endif
