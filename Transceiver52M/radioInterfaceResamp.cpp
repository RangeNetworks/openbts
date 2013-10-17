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

/* Resampling parameters for 100 MHz clocking */
#define RESAMP_INRATE			52
#define RESAMP_OUTRATE			75

/*
 * Resampling filter bandwidth scaling factor
 *   This narrows the filter cutoff relative to the output bandwidth
 *   of the polyphase resampler. At 4 samples-per-symbol using the
 *   2 pulse Laurent GMSK approximation gives us below 0.5 degrees
 *   RMS phase error at the resampler output.
 */
#define RESAMP_TX4_FILTER		0.45

#define INCHUNK				(RESAMP_INRATE * 4)
#define OUTCHUNK			(RESAMP_OUTRATE * 4)

static Resampler *upsampler = NULL;
static Resampler *dnsampler = NULL;
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
	RadioInterface::close();

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

	upsampler = NULL;
	dnsampler = NULL;
}

/* Initialize I/O specific objects */
bool RadioInterfaceResamp::init()
{
	float cutoff = 1.0f;

	close();

	if (mSPSTx == 4)
		cutoff = RESAMP_TX4_FILTER;

	dnsampler = new Resampler(RESAMP_INRATE, RESAMP_OUTRATE);
	if (!dnsampler->init()) {
		LOG(ALERT) << "Rx resampler failed to initialize";
		return false;
	}

	upsampler = new Resampler(RESAMP_OUTRATE, RESAMP_INRATE);
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
	innerSendBuffer = new signalVector(INCHUNK * 20, upsampler->len());
	outerSendBuffer = new signalVector(OUTCHUNK * 20);

	outerRecvBuffer = new signalVector(OUTCHUNK * 2, dnsampler->len());
	innerRecvBuffer = new signalVector(INCHUNK * 20);

	convertSendBuffer = new short[OUTCHUNK * 2 * 20];
	convertRecvBuffer = new short[OUTCHUNK * 2 * 2];

	sendBuffer = innerSendBuffer;
	recvBuffer = innerRecvBuffer;

	return true;
}

/* Receive a timestamped chunk from the device */
void RadioInterfaceResamp::pullBuffer()
{
	bool local_underrun;
	int rc, num_recv;
	int inner_len = INCHUNK;
	int outer_len = OUTCHUNK;

	/* Outer buffer access size is fixed */
	num_recv = mRadio->readSamples(convertRecvBuffer,
				       outer_len,
				       &overrun,
				       readTimestamp,
				       &local_underrun);
	if (num_recv != outer_len) {
		LOG(ALERT) << "Receive error " << num_recv;
		return;
	}

	convert_short_float((float *) outerRecvBuffer->begin(),
			    convertRecvBuffer, 2 * outer_len);

	underrun |= local_underrun;
	readTimestamp += (TIMESTAMP) num_recv;

	/* Write to the end of the inner receive buffer */
	rc = dnsampler->rotate((float *) outerRecvBuffer->begin(), outer_len,
			       (float *) (innerRecvBuffer->begin() + recvCursor),
			       inner_len);
	if (rc < 0) {
		LOG(ALERT) << "Sample rate upsampling error";
	}

	recvCursor += inner_len;
}

/* Send a timestamped chunk to the device */
void RadioInterfaceResamp::pushBuffer()
{
	int rc, chunks, num_sent;
	int inner_len, outer_len;

	if (sendCursor < INCHUNK)
		return;

	chunks = sendCursor / INCHUNK;
	if (chunks > 8)
		chunks = 8;

	inner_len = chunks * INCHUNK;
	outer_len = chunks * OUTCHUNK;

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
