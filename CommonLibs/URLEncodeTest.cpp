
#include "URLEncode.h"
#include <string>
#include <iostream>


using namespace std;


int main(int argc, char *argv[])
{

	string test = string("Testing: !@#$%^&*() " __DATE__ " " __TIME__);
	cout << test << endl;
	cout << URLEncode(test) << endl;
}

