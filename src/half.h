#ifndef HALF_H
#define HALF_H

#define half _Float16
#define HALF_MAX 65504

#define PG_GETARG_HALF(n)  DatumGetHalf(PG_GETARG_DATUM(n))
#define PG_RETURN_HALF(x)  return HalfGetDatum(x)

#endif
