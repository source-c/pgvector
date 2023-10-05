#ifndef HALF_H
#define HALF_H

#define __STDC_WANT_IEC_60559_TYPES_EXT__

#include <float.h>

/* _Float16 and __fp16 are not supported on x86_64 with GCC 11 */
#define half _Float16
#define HALF_MAX FLT16_MAX

#define PG_GETARG_HALF(n)  DatumGetHalf(PG_GETARG_DATUM(n))
#define PG_RETURN_HALF(x)  return HalfGetDatum(x)

#endif
