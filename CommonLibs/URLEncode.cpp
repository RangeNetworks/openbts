/* Copyright 2011, Range Networks, Inc. */

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
	for (size_t i=0; i<c.length(); i++)
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

