/*
 * Rational Sample Rate Conversion
 * Copyright (C) 2012, 2013  Thomas Tsou <tom@tsou.cc>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <malloc.h>
#include <iostream>

#include "Resampler.h"

extern "C" {
#include "convolve.h"
}

#ifndef M_PI
#define M_PI			3.14159265358979323846264338327f
#endif

#define MAX_OUTPUT_LEN		4096

static float sinc(float x)
{
	if (x == 0.0)
		return 0.9999999999;

	return sin(M_PI * x) / (M_PI * x);
}

bool Resampler::initFilters(float bw)
{
	size_t proto_len = p * filt_len;
	float *proto, val, cutoff;
	float sum = 0.0f, scale = 0.0f;
	float midpt = (float) (proto_len - 1.0) / 2.0;

	/* 
	 * Allocate partition filters and the temporary prototype filter
	 * according to numerator of the rational rate. Coefficients are
	 * real only and must be 16-byte memory aligned for SSE usage.
	 */
	proto = new float[proto_len];
	if (!proto)
		return false;

	partitions = (float **) malloc(sizeof(float *) * p);
	if (!partitions) {
		free(proto);
		return false;
	}

	for (size_t i = 0; i < p; i++) {
		partitions[i] = (float *)
				memalign(16, filt_len * 2 * sizeof(float));
	}

	/* 
	 * Generate the prototype filter with a Blackman-harris window.
	 * Scale coefficients with DC filter gain set to unity divided
	 * by the number of filter partitions. 
	 */
	float a0 = 0.35875;
	float a1 = 0.48829;
	float a2 = 0.14128;
	float a3 = 0.01168;

	if (p > q)
		cutoff = (float) p;
	else
		cutoff = (float) q;

	for (size_t i = 0; i < proto_len; i++) {
		proto[i] = sinc(((float) i - midpt) / cutoff * bw);
		proto[i] *= a0 -
			    a1 * cos(2 * M_PI * i / (proto_len - 1)) +
			    a2 * cos(4 * M_PI * i / (proto_len - 1)) -
			    a3 * cos(6 * M_PI * i / (proto_len - 1));
		sum += proto[i];
	}
	scale = p / sum;

	/* Populate filter partitions from the prototype filter */
	for (size_t i = 0; i < filt_len; i++) {
		for (size_t n = 0; n < p; n++) {
			partitions[n][2 * i + 0] = proto[i * p + n] * scale;
			partitions[n][2 * i + 1] = 0.0f;
		}
	}

	/* For convolution, we store the filter taps in reverse */ 
	for (size_t n = 0; n < p; n++) {
		for (size_t i = 0; i < filt_len / 2; i++) {
			val = partitions[n][2 * i];
			partitions[n][2 * i] = partitions[n][2 * (filt_len - 1 - i)];
			partitions[n][2 * (filt_len - 1 - i)] = val;
		}
	}

	delete proto;

	return true;
}

void Resampler::releaseFilters()
{
	if (partitions) {
		for (size_t i = 0; i < p; i++)
			free(partitions[i]);
	}

	free(partitions);
	partitions = NULL;
}

static bool check_vec_len(int in_len, int out_len, int p, int q)
{
	if (in_len % q) {
		std::cerr << "Invalid input length " << in_len
			  <<  " is not multiple of " << q << std::endl;
		return false;
	}

	if (out_len % p) {
		std::cerr << "Invalid output length " << out_len
			  <<  " is not multiple of " << p << std::endl;
		return false;
	}

	if ((in_len / q) != (out_len / p)) {
		std::cerr << "Input/output block length mismatch" << std::endl;
		std::cerr << "P = " << p << ", Q = " << q << std::endl;
		std::cerr << "Input len: " << in_len << std::endl;
		std::cerr << "Output len: " << out_len << std::endl;
		return false;
	}

	if (out_len > MAX_OUTPUT_LEN) {
		std::cerr << "Block length of " << out_len
			  << " exceeds max of " << MAX_OUTPUT_LEN << std::endl;
		return false;
	}

	return true;
}

void Resampler::computePath()
{
	for (int i = 0; i < MAX_OUTPUT_LEN; i++) {
		in_index[i] = (q * i) / p;
		out_path[i] = (q * i) % p;
	}
}

int Resampler::rotate(float *in, size_t in_len, float *out, size_t out_len)
{
	int n, path;
	int hist_len = filt_len - 1;

	if (!check_vec_len(in_len, out_len, p, q))
		return -1; 

	/* Insert history */
	memcpy(&in[-2 * hist_len], history, hist_len * 2 * sizeof(float));

	/* Generate output from precomputed input/output paths */
	for (size_t i = 0; i < out_len; i++) {
		n = in_index[i]; 
		path = out_path[i]; 

		convolve_real(in, in_len,
			      partitions[path], filt_len,
			      &out[2 * i], out_len - i,
			      n, 1, 1, 0);
	}

	/* Save history */
	memcpy(history, &in[2 * (in_len - hist_len)],
	       hist_len * 2 * sizeof(float));

	return out_len;
}

bool Resampler::init(float bw)
{
	size_t hist_len = filt_len - 1;

	/* Filterbank filter internals */
	if (initFilters(bw) < 0)
		return false;

	/* History buffer */
	history = new float[2 * hist_len];
	memset(history, 0, 2 * hist_len * sizeof(float));

	/* Precompute filterbank paths */
	in_index = new size_t[MAX_OUTPUT_LEN];
	out_path = new size_t[MAX_OUTPUT_LEN];
	computePath();

	return true;
}

size_t Resampler::len()
{
	return filt_len;
}

Resampler::Resampler(size_t p, size_t q, size_t filt_len)
	: in_index(NULL), out_path(NULL), partitions(NULL), history(NULL)
{
	this->p = p;
	this->q = q;
	this->filt_len = filt_len;
}

Resampler::~Resampler()
{
	releaseFilters();

	delete history;
	delete in_index;
	delete out_path;
}
