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



#ifndef SIPINTERFACE_H
#define SIPINTERFACE_H

#include <Globals.h>
#include <Interthread.h>
#include <Sockets.h>
#include <osip2/osip.h>

#include <string>


namespace GSM {

class L3MobileIdentity;

}


namespace SIP {


typedef InterthreadQueue<osip_message_t> _OSIPMessageFIFO;

class OSIPMessageFIFO : public _OSIPMessageFIFO {

	private:

	struct sockaddr_in mReturnAddress;

	virtual void freeElement(osip_message_t* element) const { osip_message_free(element); };

	public:

	OSIPMessageFIFO(const struct sockaddr_in* wReturnAddress)
		:_OSIPMessageFIFO()
	{
		memcpy(&mReturnAddress,wReturnAddress,sizeof(mReturnAddress));
	}

	virtual ~OSIPMessageFIFO()
	{
		// We must call clear() here, because if it is called from InterthreadQueue
		// destructor, then clear() will call InterthreadQueue's freeElement() which
		// is not what we want. This destructor behaviour is intntional, because
		// inherited object's data is already destroyed at the time parent's destructor
		// is called.
		clear();
	}

	const struct sockaddr_in* returnAddress() const { return &mReturnAddress; }

	size_t addressSize() const { return sizeof(mReturnAddress); }

};



class OSIPMessageFIFOMap : public InterthreadMap<std::string,OSIPMessageFIFO> {};


std::ostream& operator<<(std::ostream& os, const OSIPMessageFIFO& m);


/**
	A Map the keeps a SIP message FIFO for each active SIP transaction.
	Keyed by SIP call ID string.
	Overall map is thread-safe.  Each FIFO is also thread-safe.
*/
class SIPMessageMap 
{

private:

	OSIPMessageFIFOMap mMap;

public:

	/** Write sip message to the map+fifo. used by sip interface. */
	void write(const std::string& call_id, osip_message_t * sip_msg );

	/** Read sip message out of map+fifo. used by sip engine. */
	osip_message_t * read(const std::string& call_id, unsigned readTimeout=3600000);
	
	/** Create a new entry in the map. */
	bool add(const std::string& call_id, const struct sockaddr_in* returnAddress);

	/**
		Remove a fifo from map (called at the end of a sip interaction).
		@param call_id The call_id key string.
		@return True if the call_id was there in the first place.
	*/
	bool remove(const std::string& call_id);

	/** Direct access to the map. */
	// FIXME -- This should probably be replaced with more specific methods.
	OSIPMessageFIFOMap& map() {return mMap;}

};

std::ostream& operator<<(std::ostream& os, const SIPMessageMap& m);




class SIPInterface 
{

private:

	char mReadBuffer[2048];		///< buffer for UDP reads

	UDPSocket mSIPSocket;

	Mutex mSocketLock;
	Thread mDriveThread;	
	SIPMessageMap mSIPMap;	

public:
	// 2 ways to starte sip interface. 
	// Ex 1.
	// SIPInterface si;
	// si.localAdder(port0, ip_str, port1);
	// si.open(); 
	// or Ex 2.
	// SIPInterface si(port0, ip_str, port1);
	// Then after all that. si.start();


	/**
		Create the SIP interface to watch for incoming SIP messages.
	*/
	SIPInterface()
		:mSIPSocket(gConfig.getNum("SIP.Local.Port"))
	{ }

	
	/** Start the SIP drive loop. */
	void start();

	/** Receive, parse and dispatch a single SIP message. */
	void drive();

	/**
		Look for incoming INVITE messages to start MTC.
		@param msg The SIP message to check.
		@return true if the message is a new INVITE
	*/
	bool checkInvite( osip_message_t *);


	/**
		Schedule SMS for delivery.
	*/
	//void deliverSMS(const GSM::L3MobileIdentity& mobile_id, const char* returnAddress, const char* text);

	// To write a msg to outside, make the osip_message_t 
	// then call si.write(msg);
	// to read, you need to have the call_id
	// then call si.read(call_id)

	void write(const struct sockaddr_in*, osip_message_t*);

	osip_message_t* read(const std::string& call_id , unsigned readTimeout=3600000)
		{ return mSIPMap.read(call_id, readTimeout); }

	/** Create a new message FIFO in the SIP interface. */
	bool addCall(const std::string& call_id);

	bool removeCall(const std::string& call_id);

	int fifoSize(const std::string& call_id );

};

void driveLoop(SIPInterface*);


}; // namespace SIP.


/*@addtogroup Globals */
//@{
/** A single global SIPInterface in the global namespace. */
extern SIP::SIPInterface gSIPInterface;
//@}


#endif // SIPINTERFACE_H
// vim: ts=4 sw=4
