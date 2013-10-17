/*
 * Radio device interface with sample rate conversion
 * Written by Thomas Tsou <tom@tsou.cc>
 *
 * Copyright 2011, 2012, 2013 Free Software Foundation, Inc.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 * See the COPYING file in the main directory for details.
 */

#include <radioInterface.h>
#include <Logger.h>

#include "Resampler.h"

extern "C" {
#include "convert.h"
}

/* Resampling parameters for 64 MHz clocking */
#define RESAMP_64M_INRATE			65
#define RESAMP_64M_OUTRATE			96

/* Resampling parameters for 100 MHz clocking */
#define RESAMP_100M_INRATE			52
#define RESAMP_100M_OUTRATE			75

/* Universal resampling parameters */
#define NUMCHUNKS				24

/*
 * Resampling filter bandwidth scaling factor
 *   This narrows the filter cutoff relative to the output bandwidth
 *   of the polyphase resampler. At 4 samples-per-symbol using the
 *   2 pulse Laurent GMSK approximation gives us below 0.5 degrees
 *   RMS phase error at the resampler output.
 */
#define RESAMP_TX4_FILTER		0.45

static Resampler *upsampler = NULL;
static Resampler *dnsampler = NULL;
static int resamp_inrate = 0;
static int resamp_inchunk = 0;
static int resamp_outrate = 0;
static int resamp_outchunk = 0;

short *convertRecvBuffer = NULL;
short *convertSendBuffer = NULL;

RadioInterfaceResamp::RadioInterfaceResamp(RadioDevice *wRadio,
					   int wReceiveOffset,
					   int wSPS,
					   GSM::Time wStartTime)
	: RadioInterface(wRadio, wReceiveOffset, wSPS, wStartTime),
	  innerSendBuffer(NULL), outerSendBuffer(NULL),
	  innerRecvBuffer(NULL), outerRecvBuffer(NULL)
{
}

RadioInterfaceResamp::~RadioInterfaceResamp()
{
	close();
}

void RadioInterfaceResamp::close()
{
	delete innerSendBuffer;
	delete outerSendBuffer;
	delete innerRecvBuffer;
	delete outerRecvBuffer;

	delete upsampler;
	delete dnsampler;

	innerSendBuffer = NULL;
	outerSendBuffer = NULL;
	innerRecvBuffer = NULL;
	outerRecvBuffer = NULL;
	sendBuffer = NULL;
	recvBuffer = NULL;

	upsampler = NULL;
	dnsampler = NULL;

	RadioInterface::close();
}

/* Initialize I/O specific objects */
bool RadioInterfaceResamp::init(int type)
{
	float cutoff = 1.0f;

	close();

	switch (type) {
	case RadioDevice::RESAMP_64M:
		resamp_inrate = RESAMP_64M_INRATE;
		resamp_outrate = RESAMP_64M_OUTRATE;
		break;
	case RadioDevice::RESAMP_100M:
		resamp_inrate = RESAMP_100M_INRATE;
		resamp_outrate = RESAMP_100M_OUTRATE;
		break;
	case RadioDevice::NORMAL:
	default:
		LOG(ALERT) << "Invalid device configuration";
		return false;
	}

	resamp_inchunk = resamp_inrate * 4;
	resamp_outchunk = resamp_outrate * 4;

	if (resamp_inchunk  * NUMCHUNKS < 157 * mSPSTx * 2) {
		LOG(ALERT) << "Invalid inner chunk size " << resamp_inchunk;
		return false;
	}

	if (mSPSTx == 4)
		cutoff = RESAMP_TX4_FILTER;

	dnsampler = new Resampler(resamp_inrate, resamp_outrate);
	if (!dnsampler->init()) {
		LOG(ALERT) << "Rx resampler failed to initialize";
		return false;
	}

	upsampler = new Resampler(resamp_outrate, resamp_inrate);
	if (!upsampler->init(cutoff)) {
		LOG(ALERT) << "Tx resampler failed to initialize";
		return false;
	}

	/*
	 * Allocate high and low rate buffers. The high rate receive
	 * buffer and low rate transmit vectors feed into the resampler
	 * and requires headroom equivalent to the filter length. Low
	 * rate buffers are allocated in the main radio interface code.
	 */
	innerSendBuffer =
		new signalVector(NUMCHUNKS * resamp_inchunk, upsampler->len());
	outerSendBuffer =
		new signalVector(NUMCHUNKS * resamp_outchunk);
	outerRecvBuffer =
		new signalVector(resamp_outchunk, dnsampler->len());
	innerRecvBuffer =
		new signalVector(NUMCHUNKS * resamp_inchunk / mSPSTx);

	convertSendBuffer = new short[outerSendBuffer->size() * 2];
	convertRecvBuffer = new short[outerRecvBuffer->size() * 2];

	sendBuffer = innerSendBuffer;
	recvBuffer = innerRecvBuffer;

	return true;
}

/* Receive a timestamped chunk from the device */
void RadioInterfaceResamp::pullBuffer()
{
	bool local_underrun;
	int rc, num_recv;

	if (recvCursor > innerRecvBuffer->size() - resamp_inchunk)
		return;

	/* Outer buffer access size is fixed */
	num_recv = mRadio->readSamples(convertRecvBuffer,
				       resamp_outchunk,
				       &overrun,
				       readTimestamp,
				       &local_underrun);
	if (num_recv != resamp_outchunk) {
		LOG(ALERT) << "Receive error " << num_recv;
		return;
	}

	convert_short_float((float *) outerRecvBuffer->begin(),
			    convertRecvBuffer, 2 * resamp_outchunk);

	underrun |= local_underrun;
	readTimestamp += (TIMESTAMP) resamp_outchunk;

	/* Write to the end of the inner receive buffer */
	rc = dnsampler->rotate((float *) outerRecvBuffer->begin(),
			       resamp_outchunk,
			       (float *) (innerRecvBuffer->begin() + recvCursor),
			       resamp_inchunk);
	if (rc < 0) {
		LOG(ALERT) << "Sample rate upsampling error";
	}

	recvCursor += resamp_inchunk;
}

/* Send a timestamped chunk to the device */
void RadioInterfaceResamp::pushBuffer()
{
	int rc, chunks, num_sent;
	int inner_len, outer_len;

	if (sendCursor < resamp_inchunk)
		return;

	if (sendCursor > innerSendBuffer->size())
		LOG(ALERT) << "Send buffer overflow";

	chunks = sendCursor / resamp_inchunk;

	inner_len = chunks * resamp_inchunk;
	outer_len = chunks * resamp_outchunk;

	/* Always send from the beginning of the buffer */
	rc = upsampler->rotate((float *) innerSendBuffer->begin(), inner_len,
			       (float *) outerSendBuffer->begin(), outer_len);
	if (rc < 0) {
		LOG(ALERT) << "Sample rate downsampling error";
	}

	convert_float_short(convertSendBuffer,
			    (float *) outerSendBuffer->begin(),
			    powerScaling, 2 * outer_len);

	num_sent = mRadio->writeSamples(convertSendBuffer,
					outer_len,
					&underrun,
					writeTimestamp);
	if (num_sent != outer_len) {
		LOG(ALERT) << "Transmit error " << num_sent;
	}

	/* Shift remaining samples to beginning of buffer */
	memmove(innerSendBuffer->begin(),
		innerSendBuffer->begin() + inner_len,
		(sendCursor - inner_len) * 2 * sizeof(float));

	writeTimestamp += outer_len;
	sendCursor -= inner_len;
	assert(sendCursor >= 0);
}
