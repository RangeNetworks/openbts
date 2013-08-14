// Pat added this file.
// We need an include file that is included before any other include files.
// Might I suggest that Range Networks specific global #defines be prefixed with RN_

#ifndef DEFINES_H
#define DEFINES_H

// GPRS_1 turns on the SharedEncoder.  It is the thing that keeps the modem from registering.
#define GPRS_ENCODER 1	// Use SharedL1Encoder and SharedL1Decoder
#define GPRS_TESTSI4 1
#define GPRS_TEST 1		// Compile in other GPRS stuff.
#define GPRS_PAT 1		// Compile in GPRS code.  Turn this off to get previous non-GRPS code,
						// although I am not maintaining it so you may have to fix compile
						// problems to use it.

// __GNUG__ is true for g++ and __GNUC__ for gcc.
#if __GNUC__&0==__GNUG__

#define RN_UNUSED __attribute__((unused))

#define RN_UNUSED_PARAM(var) RN_UNUSED var

// Pack structs onto byte boundaries.
// Note that if structs are nested, this must appear on all of them.
#define RN_PACKED __attribute__((packed))

#else

// Suppress warning message about a variable or function being unused.
// In C++ you can leave out the variable name to suppress the 'unused variable' warning.
#define RN_UNUSED_PARAM(var)	/*nothing*/
#define RN_UNUSED		/*not defined*/
#define RN_PACKED 		/*not defined*/
#endif

// Bound value between min and max values.
#define RN_BOUND(value,min,max) ( (value)<(min) ? (min) : (value)>(max) ? (max) : (value) )

#define RN_PRETTY_TEXT(name) (" " #name "=(") << name << ")"
#define RN_PRETTY_TEXT1(name) (" " #name "=") << name
#define RN_WRITE_TEXT(name) os << RN_PRETTY_TEXT(name)
#define RN_WRITE_OPT_TEXT(name,flag) if (flag) { os << RN_WRITE_TEXT(name); }

#endif
