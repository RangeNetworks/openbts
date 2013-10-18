/*
* Copyright 2013 Range Networks, Inc.
*
* This software is distributed under multiple licenses;
* see the COPYING file in the main directory for licensing
* information for this specific distribuion.
*
* This use of this software may be subject to additional restrictions.
* See the LEGAL file in the main directory for details.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

*/


#include <FactoryCalibration.h>


int FactoryCalibration::hexval (char ch)
{
	if ('0' <= ch && ch <= '9') {
		return ch - '0';
	}

	if ('a' <= ch && ch <= 'f') {
		return ch - 'a' + 10;
	}

	if ('A' <= ch && ch <= 'F') {
		return ch - 'A' + 10;
	}

	return -1;
}

unsigned char * FactoryCalibration::hex_string_to_binary(const char *string, int *lenptr)
{
	int sl = strlen (string);
	if (sl & 0x01){
//		fprintf (stderr, "%s: odd number of chars in <hex-string>\n", prog_name);
		return 0;
	}

	int len = sl / 2;
	*lenptr = len;
	unsigned char *buf = new unsigned char [len];

	for (int i = 0; i < len; i++){
		int hi = hexval (string[2 * i]);
		int lo = hexval (string[2 * i + 1]);
		if (hi < 0 || lo < 0){
//			fprintf (stderr, "%s: invalid char in <hex-string>\n", prog_name);
			delete [] buf;
			return 0;
		}
		buf[i] = (hi << 4) | lo;
	}
	return buf;
}

bool FactoryCalibration::i2c_write(int i2c_addr, char *hex_string)
{
	int len = 0;
	unsigned char *buf = hex_string_to_binary (hex_string, &len);
	if (buf == 0) {
		return false;
	}
	return core->writeI2c(i2c_addr, buf, len);
}

std::string FactoryCalibration::i2c_read(int i2c_addr, int len)
{
	unsigned char *buf = new unsigned char [len];
	bool result = core->readI2c(i2c_addr, buf, len);
	if (!result) {
		return "";
	}

	char hex[64];
	for (int i = 0; i < len; i++){
		sprintf (hex+(2*i), "%02x", buf[i]);
	}

	return std::string(hex);
}

unsigned int FactoryCalibration::hex2dec(std::string hex)
{
	unsigned int dec;
	std::stringstream tempss;

	tempss << std::hex << hex;
	tempss >> dec;

	return dec;
}

unsigned int FactoryCalibration::getValue(std::string name) {
	if (name.compare("sdrsn")==0) {
		return sdrsn;

	} else if (name.compare("rfsn")==0) {
		return rfsn;

	// TODO : (mike) I thought these should be DEC comparisons but only HEX vals are matching,
	//		too rushed for 3.1 to figure out why I'm dumb
	} else if (name.compare("band")==0) {
		// BAND_85="85" # dec 133
		if (band == 85) {
			return 850;

		// BAND_90="90" # dec 144
		} else if (band == 90) {
			return 900;

		// BAND_18="18" # dec 24
		} else if (band == 18) {
			return 1800;

		// BAND_19="19" # dec 25
		} else if (band == 19) {
			return 1900;

		// BAND_21="21" # dec 33
		} else if (band == 21) {
			return 2100;

		// BAND_MB="ab" # dec 171
		} else if (band == 0xab) {
			return 0;

		// TODO : anything to handle here? for now pretend they have a multi-band
		} else {
			return 0;
		}

	} else if (name.compare("freq")==0) {
		return freq;

	} else if (name.compare("rxgain")==0) {
		return rxgain;

	} else if (name.compare("txgain")==0) {
		return txgain;

	// TODO : need a better error condition here
	} else {
		return 0;
	}
}

void FactoryCalibration::readEEPROM(int deviceID) {

	core = new rnrad1Core(
		deviceID,
		RAD1_CMD_INTERFACE,
		RAD1_CMD_ALTINTERFACE,
		"fpga.rbf",
		"ezusb.ihx",
		false
	);

	bool ret;
	std::string temp;
	std::string temp1;

	/*
	SDR_ADDR="0x50"
	BASEST="df"
	MKR_ST="ff"
	./RAD1Cmd i2c_write $SDR_ADDR $BASEST$MKR_ST
	sleep 1
	*/
	ret = i2c_write(0x50, "dfff");
	sleep(1);
//	std::cout << "i2c_write = " << ret << std::endl;

	/*
	TEMP=$( ./RAD1Cmd i2c_read $SDR_ADDR 16 )
	sleep 1
	*/
	temp = i2c_read(0x50, 16);
	sleep(1);
//	std::cout << "i2c_read 16 = " << temp << std::endl;

	/*
	TEMP=$( ./RAD1Cmd i2c_read $SDR_ADDR 32 )
	*/
	temp = i2c_read(0x50, 32);
//	std::cout << "i2c_read 32 = " << temp << std::endl;

	/*
	# parse SDR serial number
	TEMP1=$( echo "$TEMP" | cut -c 1-4  )
	SDRSN=$( printf '%d' 0x$TEMP1 )
	echo "SDR Serial Number [$SDRSN] hex was [$TEMP1]"
	*/
	temp1 = temp.substr(0, 4);
	sdrsn = hex2dec(temp1);
//	std::cout << "SDR Serial Number [" << sdrsn << "] hex was [" << temp1 << "]" << std::endl;

	/*
	# parse RF serial number
	TEMP1=$( echo "$TEMP" | cut -c 5-8  )
	RFSN=$( printf '%d' 0x$TEMP1 )
	echo "RF Serial Number [$RFSN] hex was [$TEMP1]"
	*/
	temp1 = temp.substr(4, 4);
	rfsn = hex2dec(temp1);
//	std::cout << "RF Serial Number [" << rfsn << "] hex was [" << temp1 << "]" << std::endl;

	/*
	# BAND
	BAND=$( echo "$TEMP" | cut -c 9-10  )
	echo "RF BAND [$BAND]"
	*/
	temp1 = temp.substr(8, 2);
	band = atoi(temp1.c_str());
//	std::cout << "RF BAND [" << band << "]" << std::endl;

	/*
	# Frequency Setting
	TEMP1=$( echo "$TEMP" | cut -c 11-12  )
	FREQ=$( printf '%d' 0x$TEMP1 )
	echo "FREQ [$FREQ]"
	*/
	temp1 = temp.substr(10, 2);
	freq = hex2dec(temp1);
//	std::cout << "FREQ [" << freq << "] hex was [" << temp1 << "]" << std::endl;

	/*
	# RxGAIN
	TEMP1=$( echo "$TEMP" | cut -c 13-14  )
	RXGN=$( printf '%d' 0x$TEMP1 )
	echo "RxGAIN [$RXGN]"
	*/
	temp1 = temp.substr(12, 2);
	rxgain = hex2dec(temp1);
//	std::cout << "RxGAIN [" << rxgain << "] hex was [" << temp1 << "]" << std::endl;

	/*
	# TxGAIN
	TEMP1=$( echo "$TEMP" | cut -c 15-16  )
	TXGN=$( printf '%d' 0x$TEMP1 )
	echo "ATTEN [$TXGN]"
	*/
	temp1 = temp.substr(14, 2);
	txgain = hex2dec(temp1);
//	std::cout << "TxGAIN/ATTEN [" << txgain << "] hex was [" << temp1 << "]" << std::endl;

	core->~rnrad1Core();

	return;
}
