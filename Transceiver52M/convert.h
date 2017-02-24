#ifndef _CONVERT_H_
#define _CONVERT_H_

void convert_float_short(short *out, float *in, float scale, int len);
void convert_short_float(float *out, short *in, int len);

#endif /* _CONVERT_H_ */
