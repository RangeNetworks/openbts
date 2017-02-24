/*
 * SSE type conversions
 * Copyright (C) 2013 Thomas Tsou <tom@tsou.cc>
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

#include <malloc.h>
#include <string.h>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef HAVE_SSE3
#include <xmmintrin.h>
#include <emmintrin.h>

#ifdef HAVE_SSE4_1
#include <smmintrin.h>

/* 16*N 16-bit signed integer converted to single precision floats */
static void _sse_convert_si16_ps_16n(float *restrict out,
				     short *restrict in,
				     int len)
{
	__m128i m0, m1, m2, m3, m4, m5;
	__m128 m6, m7, m8, m9;

	for (int i = 0; i < len / 16; i++) {
		/* Load (unaligned) packed floats */
		m0 = _mm_loadu_si128((__m128i *) &in[16 * i + 0]);
		m1 = _mm_loadu_si128((__m128i *) &in[16 * i + 8]);

		/* Unpack */
		m2 = _mm_cvtepi16_epi32(m0);
		m4 = _mm_cvtepi16_epi32(m1);
		m0 = _mm_shuffle_epi32(m0, _MM_SHUFFLE(1, 0, 3, 2));
		m1 = _mm_shuffle_epi32(m1, _MM_SHUFFLE(1, 0, 3, 2));
		m3 = _mm_cvtepi16_epi32(m0);
		m5 = _mm_cvtepi16_epi32(m1);

		/* Convert */
		m6 = _mm_cvtepi32_ps(m2);
		m7 = _mm_cvtepi32_ps(m3);
		m8 = _mm_cvtepi32_ps(m4);
		m9 = _mm_cvtepi32_ps(m5);

		/* Store */
		_mm_storeu_ps(&out[16 * i + 0], m6);
		_mm_storeu_ps(&out[16 * i + 4], m7);
		_mm_storeu_ps(&out[16 * i + 8], m8);
		_mm_storeu_ps(&out[16 * i + 12], m9);
	}
}

/* 16*N 16-bit signed integer conversion with remainder */
static void _sse_convert_si16_ps(float *restrict out,
				 short *restrict in,
				 int len)
{
	int start = len / 16 * 16;

	_sse_convert_si16_ps_16n(out, in, len);

	for (int i = 0; i < len % 16; i++)
		out[start + i] = in[start + i];
}
#endif /* HAVE_SSE4_1 */

/* 8*N single precision floats scaled and converted to 16-bit signed integer */
static void _sse_convert_scale_ps_si16_8n(short *restrict out,
					  float *restrict in,
					  float scale, int len)
{
	__m128 m0, m1, m2;
	__m128i m4, m5;

	for (int i = 0; i < len / 8; i++) {
		/* Load (unaligned) packed floats */
		m0 = _mm_loadu_ps(&in[8 * i + 0]);
		m1 = _mm_loadu_ps(&in[8 * i + 4]);
		m2 = _mm_load1_ps(&scale);

		/* Scale */
		m0 = _mm_mul_ps(m0, m2);
		m1 = _mm_mul_ps(m1, m2);

		/* Convert */
		m4 = _mm_cvtps_epi32(m0);
		m5 = _mm_cvtps_epi32(m1);

		/* Pack and store */
		m5 = _mm_packs_epi32(m4, m5);
		_mm_storeu_si128((__m128i *) &out[8 * i], m5);
	}
}

/* 8*N single precision floats scaled and converted with remainder */
static void _sse_convert_scale_ps_si16(short *restrict out,
				       float *restrict in,
				       float scale, int len)
{
	int start = len / 8 * 8;

	_sse_convert_scale_ps_si16_8n(out, in, scale, len);

	for (int i = 0; i < len % 8; i++)
		out[start + i] = in[start + i] * scale;
}

/* 16*N single precision floats scaled and converted to 16-bit signed integer */
static void _sse_convert_scale_ps_si16_16n(short *restrict out,
					   float *restrict in,
					   float scale, int len)
{
	__m128 m0, m1, m2, m3, m4;
	__m128i m5, m6, m7, m8;

	for (int i = 0; i < len / 16; i++) {
		/* Load (unaligned) packed floats */
		m0 = _mm_loadu_ps(&in[16 * i + 0]);
		m1 = _mm_loadu_ps(&in[16 * i + 4]);
		m2 = _mm_loadu_ps(&in[16 * i + 8]);
		m3 = _mm_loadu_ps(&in[16 * i + 12]);
		m4 = _mm_load1_ps(&scale);

		/* Scale */
		m0 = _mm_mul_ps(m0, m4);
		m1 = _mm_mul_ps(m1, m4);
		m2 = _mm_mul_ps(m2, m4);
		m3 = _mm_mul_ps(m3, m4);

		/* Convert */
		m5 = _mm_cvtps_epi32(m0);
		m6 = _mm_cvtps_epi32(m1);
		m7 = _mm_cvtps_epi32(m2);
		m8 = _mm_cvtps_epi32(m3);

		/* Pack and store */
		m5 = _mm_packs_epi32(m5, m6);
		m7 = _mm_packs_epi32(m7, m8);
		_mm_storeu_si128((__m128i *) &out[16 * i + 0], m5);
		_mm_storeu_si128((__m128i *) &out[16 * i + 8], m7);
	}
}
#else /* HAVE_SSE3 */
static void convert_scale_ps_si16(short *out, float *in, float scale, int len)
{
	for (int i = 0; i < len; i++)
		out[i] = in[i] * scale;
}
#endif

#ifndef HAVE_SSE4_1
static void convert_si16_ps(float *out, short *in, int len)
{
	for (int i = 0; i < len; i++)
		out[i] = in[i];
}
#endif

void convert_float_short(short *out, float *in, float scale, int len)
{
#ifdef HAVE_SSE3
	if (!(len % 16))
		_sse_convert_scale_ps_si16_16n(out, in, scale, len);
	else if (!(len % 8))
		_sse_convert_scale_ps_si16_8n(out, in, scale, len);
	else
		_sse_convert_scale_ps_si16(out, in, scale, len);
#else
	convert_scale_ps_si16(out, in, scale, len);
#endif
}

void convert_short_float(float *out, short *in, int len)
{
#ifdef HAVE_SSE4_1
	if (!(len % 16))
		_sse_convert_si16_ps_16n(out, in, len);
	else
		_sse_convert_si16_ps(out, in, len);
#else
	convert_si16_ps(out, in, len);
#endif
}
