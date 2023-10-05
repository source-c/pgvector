#include "postgres.h"

#include "common/shortest_dec.h"
#include "fmgr.h"
#include "half.h"
#include "lib/stringinfo.h"
#include "libpq/pqformat.h"
#include "utils/array.h"
#include "utils/builtins.h"
#include "utils/float.h"
#include "utils/numeric.h"

#if PG_VERSION_NUM < 120003
static pg_noinline void
float_overflow_error(void)
{
	ereport(ERROR,
			(errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
			 errmsg("value out of range: overflow")));
}

static pg_noinline void
float_underflow_error(void)
{
	ereport(ERROR,
			(errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
			 errmsg("value out of range: underflow")));
}
#endif

/*
 * Check if array is a vector
 */
static bool
ArrayIsVector(ArrayType *a)
{
	return ARR_NDIM(a) == 1 && !array_contains_nulls(a);
}

/*
 * Check if dimensions are the same
 */
static int
CheckDims(ArrayType *a, ArrayType *b)
{
	int			dima;
	int			dimb;

	if (!ArrayIsVector(a) || !ArrayIsVector(b))
		return 0;

	dima = ARR_DIMS(a)[0];
	dimb = ARR_DIMS(b)[0];

	if (dima != dimb)
		return 0;

	return dima;
}

/*
 * Return the datum representation for a half
 */
static inline Datum
HalfGetDatum(half X)
{
	union
	{
		half		value;
		int16		retval;
	}			myunion;

	myunion.value = X;
	return Int16GetDatum(myunion.retval);
}

/*
 * Return the half value of a datum
 */
static inline half
DatumGetHalf(Datum X)
{
	union
	{
		int16		value;
		half		retval;
	}			myunion;

	myunion.value = DatumGetInt16(X);
	return myunion.retval;
}

/*
 * Append a half to a StringInfo buffer
 */
static half
pq_getmsghalf(StringInfo msg)
{
	union
	{
		half		h;
		uint16		i;
	}			swap;

	swap.i = pq_getmsgint(msg, 2);
	return swap.h;
}

/*
 * Get a half from a message buffer
 */
static void
pq_sendhalf(StringInfo buf, half h)
{
	union
	{
		half		h;
		uint16		i;
	}			swap;

	swap.h = h;
	pq_sendint16(buf, swap.i);
}

/*
 * Convert a float4 to a half
 */
static half
Float4ToHalf(float num)
{
	half		result;

	result = (half) num;
	if (unlikely(isinf(result)) && !isinf(num))
		float_overflow_error();
	if (unlikely(result == 0.0f) && num != 0.0)
		float_underflow_error();

	return result;
}

/*
 * Convert textual representation to internal representation
 */
PGDLLEXPORT PG_FUNCTION_INFO_V1(half_in);
Datum
half_in(PG_FUNCTION_ARGS)
{
	char	   *num = PG_GETARG_CSTRING(0);
	char	   *orig_num;
	float		val;
	char	   *endptr;

	orig_num = num;

	/* Skip leading whitespace */
	while (*num != '\0' && isspace((unsigned char) *num))
		num++;

	/*
	 * Check for an empty-string input to begin with, to avoid the vagaries of
	 * strtof() on different platforms.
	 */
	if (*num == '\0')
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_TEXT_REPRESENTATION),
				 errmsg("invalid input syntax for type %s: \"%s\"",
						"half", orig_num)));

	val = strtof(num, &endptr);

	if (val < -HALF_MAX || val > HALF_MAX)
		ereport(ERROR,
				(errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
				 errmsg("\"%s\" is out of range for type %s",
						orig_num, "half")));

	/* Skip trailing whitespace */
	while (*endptr != '\0' && isspace((unsigned char) *endptr))
		endptr++;

	/* If there is any junk left at the end of the string, bail out */
	if (*endptr != '\0')
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_TEXT_REPRESENTATION),
				 errmsg("invalid input syntax for type %s: \"%s\"",
						"half", orig_num)));

	PG_RETURN_HALF(val);
}

/*
 * Convert internal representation to textual representation
 */
PGDLLEXPORT PG_FUNCTION_INFO_V1(half_out);
Datum
half_out(PG_FUNCTION_ARGS)
{
	float		num = (float) PG_GETARG_HALF(0);
	char	   *ascii = (char *) palloc(32);
	int			ndig = FLT_DIG + extra_float_digits;

	if (extra_float_digits > 0)
	{
		float_to_shortest_decimal_buf(num, ascii);
		PG_RETURN_CSTRING(ascii);
	}

	(void) pg_strfromd(ascii, 32, ndig, num);
	PG_RETURN_CSTRING(ascii);
}

/*
 * Convert external binary representation to internal representation
 */
PGDLLEXPORT PG_FUNCTION_INFO_V1(half_recv);
Datum
half_recv(PG_FUNCTION_ARGS)
{
	StringInfo	buf = (StringInfo) PG_GETARG_POINTER(0);

	PG_RETURN_HALF(pq_getmsghalf(buf));
}

/*
 * Convert internal representation to the external binary representation
 */
PGDLLEXPORT PG_FUNCTION_INFO_V1(half_send);
Datum
half_send(PG_FUNCTION_ARGS)
{
	half		arg1 = PG_GETARG_HALF(0);
	StringInfoData buf;

	pq_begintypsend(&buf);
	pq_sendhalf(&buf, arg1);
	PG_RETURN_BYTEA_P(pq_endtypsend(&buf));
}

/*
 * Convert integer to half
 */
PGDLLEXPORT PG_FUNCTION_INFO_V1(integer_to_half);
Datum
integer_to_half(PG_FUNCTION_ARGS)
{
	int32		i = PG_GETARG_INT32(0);

	/* TODO Figure out correct error */
	float		f = (float) i;
	half		h = Float4ToHalf(f);

	PG_RETURN_HALF(h);
}

/*
 * Convert numeric to half
 */
PGDLLEXPORT PG_FUNCTION_INFO_V1(numeric_to_half);
Datum
numeric_to_half(PG_FUNCTION_ARGS)
{
	Numeric		num = PG_GETARG_NUMERIC(0);
	float		f = DatumGetFloat4(DirectFunctionCall1(numeric_float4, NumericGetDatum(num)));
	half		h = Float4ToHalf(f);

	PG_RETURN_HALF(h);
}

/*
 * Get the L2 distance between half arrays
 */
PGDLLEXPORT PG_FUNCTION_INFO_V1(half_l2_distance);
Datum
half_l2_distance(PG_FUNCTION_ARGS)
{
	ArrayType  *a = PG_GETARG_ARRAYTYPE_P(0);
	ArrayType  *b = PG_GETARG_ARRAYTYPE_P(1);
	half	   *ax = (half *) ARR_DATA_PTR(a);
	half	   *bx = (half *) ARR_DATA_PTR(b);
	half		distance = 0.0;
	int			dim = CheckDims(a, b);

	/* TODO Decide on error or NULL */
	if (!dim)
		PG_RETURN_NULL();

	/* Auto-vectorized */
	for (int i = 0; i < dim; i++)
	{
		half		diff = ax[i] - bx[i];

		distance += diff * diff;
	}

	PG_RETURN_FLOAT8(sqrt(distance));
}

/*
 * Get the inner product of two half arrays
 */
PGDLLEXPORT PG_FUNCTION_INFO_V1(half_inner_product);
Datum
half_inner_product(PG_FUNCTION_ARGS)
{
	ArrayType  *a = PG_GETARG_ARRAYTYPE_P(0);
	ArrayType  *b = PG_GETARG_ARRAYTYPE_P(1);
	half	   *ax = (half *) ARR_DATA_PTR(a);
	half	   *bx = (half *) ARR_DATA_PTR(b);
	half		distance = 0.0;
	int			dim = CheckDims(a, b);

	/* TODO Decide on error or NULL */
	if (!dim)
		PG_RETURN_NULL();

	/* Auto-vectorized */
	for (int i = 0; i < dim; i++)
		distance += ax[i] * bx[i];

	PG_RETURN_FLOAT8(distance);
}

/*
 * Get the negative inner product of two half arrays
 */
PGDLLEXPORT PG_FUNCTION_INFO_V1(half_negative_inner_product);
Datum
half_negative_inner_product(PG_FUNCTION_ARGS)
{
	ArrayType  *a = PG_GETARG_ARRAYTYPE_P(0);
	ArrayType  *b = PG_GETARG_ARRAYTYPE_P(1);
	half	   *ax = (half *) ARR_DATA_PTR(a);
	half	   *bx = (half *) ARR_DATA_PTR(b);
	half		distance = 0.0;
	int			dim = CheckDims(a, b);

	/* TODO Decide on error or NULL */
	if (!dim)
		PG_RETURN_NULL();

	/* Auto-vectorized */
	for (int i = 0; i < dim; i++)
		distance += ax[i] * bx[i];

	PG_RETURN_FLOAT8(distance * -1);
}

/*
 * Get the cosine distance between two half arrays
 */
PGDLLEXPORT PG_FUNCTION_INFO_V1(half_cosine_distance);
Datum
half_cosine_distance(PG_FUNCTION_ARGS)
{
	ArrayType  *a = PG_GETARG_ARRAYTYPE_P(0);
	ArrayType  *b = PG_GETARG_ARRAYTYPE_P(1);
	half	   *ax = (half *) ARR_DATA_PTR(a);
	half	   *bx = (half *) ARR_DATA_PTR(b);
	half		distance = 0.0;
	half		norma = 0.0;
	half		normb = 0.0;
	double		similarity;
	int			dim = CheckDims(a, b);

	/* TODO Decide on error or NULL */
	if (!dim)
		PG_RETURN_NULL();

	/* Auto-vectorized */
	for (int i = 0; i < dim; i++)
	{
		half		axi = ax[i];
		half		bxi = bx[i];

		distance += axi * bxi;
		norma += axi * axi;
		normb += bxi * bxi;
	}

	/* Use sqrt(a * b) over sqrt(a) * sqrt(b) */
	similarity = distance / sqrt(norma * normb);

#ifdef _MSC_VER
	/* /fp:fast may not propagate NaN */
	if (isnan(similarity))
		PG_RETURN_FLOAT8(NAN);
#endif

	/* Keep in range */
	if (similarity > 1)
		similarity = 1;
	else if (similarity < -1)
		similarity = -1;

	PG_RETURN_FLOAT8(1 - similarity);
}
