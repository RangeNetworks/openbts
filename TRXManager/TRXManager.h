/*
* Copyright 2008 Free Software Foundation, Inc.
* Copyright 2014 Range Networks, Inc.
*
* This software is distributed under multiple licenses; see the COPYING file in the main directory for licensing information for this specific distribution.
*
* This use of this software may be subject to additional restrictions.
* See the LEGAL file in the main directory for details.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

*/



#ifndef TRXMANAGER_H
#define TRXMANAGER_H

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "Threads.h"
#include "Sockets.h"
#include "Interthread.h"
#include "GSMCommon.h"
#include "GSMTransfer.h"
#include <list>


/* Forward refs into the GSM namespace. */
namespace GSM {

class L1Decoder;

};


class ARFCNManager;


/**
	The TransceiverManager processes the complete transcevier interface.
	There is one of these for each access point.
 */
class TransceiverManager {

	private:

	/// the ARFCN manangers under this TRX
	std::vector<ARFCNManager*> mARFCNs;		

	/// set true when the first CLOCK packet is received
	volatile bool mHaveClock;
	/// socket for clock management messages
	UDPSocket mClockSocket;		
	/// a thread to monitor the global clock socket
	Thread mClockThread;	


	public:

	/**
		Construct a TransceiverManager.
		@param numARFCNs Number of ARFCNs supported by the transceiver.
		@param wTRXAddress IP address of the transceiver.
		@param wBasePort The base port for the interface, as defined in README.TRX.
	*/
	TransceiverManager(int numARFCNs,
		const char* wTRXAddress, int wBasePort);

	/**@name Accessors. */
	//@{
	ARFCNManager* ARFCN(unsigned i) { assert(i<mARFCNs.size()); return mARFCNs.at(i); }
	//@}

	bool haveClock() const { return mHaveClock; }

	unsigned C0() const;
	unsigned numARFCNs() const { return mARFCNs.size(); }

	/** Block until the clock is set over the UDP link. */
	//void waitForClockInit() const;

	/** Start the clock management thread and all ARFCN managers. */
	void start();

	/** Clock service loop. */
	friend void* ClockLoopAdapter(TransceiverManager*);

	private:

	/** Handler for messages on the clock interface. */
	void clockHandler();
};




void* ClockLoopAdapter(TransceiverManager *TRXm);




/**
	The ARFCN Manager processes transceiver functions for a single ARFCN.
	When we do frequency hopping, this will manage a full rate radio channel.
*/
class ARFCNManager {

	private:

	TransceiverManager &mTransceiver;

	Mutex mDataSocketLock;			///< lock to prevent contentional for the socket
	UDPSocket mDataSocket;			///< socket for data transfer
	Mutex mControlLock;				///< lock to prevent overlapping transactions
	UDPSocket mControlSocket;		///< socket for radio control

	Thread mRxThread;				///< thread to receive data from rx

	/**@name The demux table. */
	//@{
	Mutex mTableLock;
	static const unsigned maxModulus=51*26*4;	///< maximum unified repeat period
	GSM::L1Decoder* mDemuxTable[8][maxModulus];		///< the demultiplexing table for received bursts
	//@}

	unsigned mARFCN;						///< the current ARFCN


	public:

	ARFCNManager(const char* wTRXAddress, int wBasePort, TransceiverManager &wTRX);

	/** Start the uplink thread. */
	void start();

	unsigned ARFCN() const { return mARFCN; }

	 // (pat) This passes the message through to UDPSocket::write(),
	 // which maps to DatagramSocket::write() which does an immediate sendto() on the socket.
	 // (pat) Renamed overloaded function to clarify code.
	 // Culprit says who called us, for debugging.
	void writeHighSideTx(const GSM::TxBurst& burst,const char *culprit);


	/**@name Transceiver controls. */
	//@{

	/**
		Tune to a given ARFCN.
		@param wARFCN Target for tuning.
		@return true on success.
	*/
	bool tune(int wARFCN);

	/**
		Tune to a given ARFCN, but with rx and tx on the same (downlink) frequency.
		@param wARFCN Target for tuning, using downlink frequeny.
		@return true on success.
	*/
	bool tuneLoopback(int wARFCN);

	/** Turn off the transceiver. */
	bool powerOff();

	/** Turn on the transceiver. */
	
	/**
		Turn on the transceiver.
		@param warn Warn if the transceiver fails to start
	*/
	bool powerOn(bool warn);

	/** Just test if the transceiver is running without printing alarming messages. */
	bool trxRunning();

        /**     
		Set maximum expected delay spread.
		@param km Max network range in kilometers.
		@return true on success.
        */
        bool setMaxDelay(unsigned km);

        /**     
                Set radio receive gain.
                @param new desired gain in dB.
                @return new gain in dB.
        */
        signed setRxGain(signed dB);

        /**     
                Set radio transmit attenuation
                @param new desired attenuation in dB.
                @return new attenuation in dB.
        */
        signed setTxAtten(signed dB);

        /**     
                Set radio frequency offset
                @param new desired freq. offset
                @return new freq. offset
        */
        signed setFreqOffset(signed offset);


        /**
                Get noise level as RSSI.
                @return current noise level.
        */
        signed getNoiseLevel(void);

	/**
	Get factory calibration values.
	@return current eeprom values.
	*/
	signed getFactoryCalibration(const char * param);

	/**
		Set power wrt full scale.
		@param dB Power level wrt full power.
		@return true on success.
	*/
	bool setPower(int dB);

	/**
		Set TSC for all slots on the ARFCN.
		@param TSC TSC to use.
		@return true on success.
	*/
	bool setTSC(unsigned TSC);

 	/**
		Set the BSIC (including setting TSC for all slots on the ARFCN).
		@param BSIC BSIC to use.
		@return true on success.
	*/
	bool setBSIC(unsigned BSIC);

	/**
		Describe the channel combination on a given slot.
		@param TN The timeslot number 0..7.
		@param combo Channel combination, GSM 05.02 6.4.1, 0 for inactive.
		@return true on success.
	*/
	bool setSlot(unsigned TN, unsigned combo);

	/**
		Set the given slot to run the handover burst correlator.
		@param TN The timeslot number.
		@return true on succes.
	*/
	bool setHandover(unsigned TN);

	/**
		Clear the given slot to run the handover burst correlator.
		@param TN The timeslot number.
		@return true on success.
	*/
	bool clearHandover(unsigned TN);

	//@}


	/** Install a decoder on this ARFCN. */
	void installDecoder(GSM::L1Decoder* wL1);



	private:

	/** Action for reception. */
	void driveRx();

	/** Demultiplex and process a received burst. */
	void receiveBurst(const GSM::RxBurst&);

	/** Receiver loop. */
	friend void* ReceiveLoopAdapter(ARFCNManager*);

	/**
		Send a command packet and get the response packet.
		@param command The NULL-terminated command string to send.
		@param response A buffer for the response packet, assumed to be char[MAX_PACKET_LENGTH].
		@return Length of the response or -1 on failure.
	*/
	int sendCommandPacket(const char* command, char* response);

	/**
		Send a command with a parameter.
		@param command The command name.
		@param param The parameter for the command.
		@param responseParam Optional parameter returned
		@return The status code, 0 on success, -1 on local failure.
	*/
	int sendCommand(const char*command, const char*param, int *responseParam);

	/**
		Send a command with a parameter.
		@param command The command name.
		@param param The parameter for the command.
                @param responseParam Optional parameter returned
		@return The status code, 0 on success, -1 on local failure.
	*/
	int sendCommand(const char* command, int param, int *responseParam=NULL);

	/**
		Send a command with a string parameter.
		@param command The command name.
		@param param The string parameter(s).
		@return The status code, 0 on success, -1 on local failure.
	*/
	int sendCommand(const char* command, const char* param);


	/**
		Send a command with no parameter.
		@param command The command name.
		@return The status code, 0 on success, -1 on local failure.
	*/
	int sendCommand(const char* command);


};


/** C interface for ARFCNManager threads. */
void* ReceiveLoopAdapter(ARFCNManager*);


#endif
// vim: ts=4 sw=4
