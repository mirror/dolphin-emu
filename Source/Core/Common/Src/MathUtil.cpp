// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#include "MathUtil.h"

#include <cmath>
#include <numeric>

namespace MathUtil
{

u32 ClassifyDouble(double dvalue)
{
	// TODO: Optimize the below to be as fast as possible.
	IntDouble value;
	value.d = dvalue;
	u64 sign = value.i & DOUBLE_SIGN;
	u64 exp  = value.i & DOUBLE_EXP;
	if (exp > DOUBLE_ZERO && exp < DOUBLE_EXP) 
	{
		// Nice normalized number.
		return sign ? PPC_FPCLASS_NN : PPC_FPCLASS_PN;
	}
	else
	{
		u64 mantissa = value.i & DOUBLE_FRAC;
		if (mantissa)
		{
			if (exp)
			{
				return PPC_FPCLASS_QNAN;
			}
			else
			{
				// Denormalized number.
				return sign ? PPC_FPCLASS_ND : PPC_FPCLASS_PD;
			}
		}
		else if (exp)
		{
			//Infinite
			return sign ? PPC_FPCLASS_NINF : PPC_FPCLASS_PINF;
		}
		else
		{
			//Zero
			return sign ? PPC_FPCLASS_NZ : PPC_FPCLASS_PZ;
		}
	}
}

u32 ClassifyFloat(float fvalue)
{
	// TODO: Optimize the below to be as fast as possible.
	IntFloat value;
	value.f = fvalue;
	u32 sign = value.i & FLOAT_SIGN;
	u32 exp  = value.i & FLOAT_EXP;
	if (exp > FLOAT_ZERO && exp < FLOAT_EXP) 
	{
		// Nice normalized number.
		return sign ? PPC_FPCLASS_NN : PPC_FPCLASS_PN;
	}
	else
	{
		u32 mantissa = value.i & FLOAT_FRAC;
		if (mantissa)
		{
			if (exp)
			{
				return PPC_FPCLASS_QNAN; // Quiet NAN
			}
			else
			{
				// Denormalized number.
				return sign ? PPC_FPCLASS_ND : PPC_FPCLASS_PD;
			}
		} 
		else if (exp) 
		{
			// Infinite
			return sign ? PPC_FPCLASS_NINF : PPC_FPCLASS_PINF;
		} 
		else 
		{
			//Zero
			return sign ? PPC_FPCLASS_NZ : PPC_FPCLASS_PZ;
		}
	}
}


}  // namespace

inline void MatrixMul(int n, const float *a, const float *b, float *result)
{
	for (int i = 0; i < n; ++i)
	{
		for (int j = 0; j < n; ++j)
		{
			float temp = 0;
			for (int k = 0; k < n; ++k)
			{
				temp += a[i * n + k] * b[k * n + j];
			}
			result[i * n + j] = temp;
		}
	}
}

// Calculate sum of a float list
float MathFloatVectorSum(const std::vector<float>& Vec)
{
	return std::accumulate(Vec.begin(), Vec.end(), 0.0f);
}

void Matrix33::LoadIdentity(Matrix33 &mtx)
{
	memset(mtx.data, 0, sizeof(mtx.data));
	mtx.data[0] = 1.0f;
	mtx.data[4] = 1.0f;
	mtx.data[8] = 1.0f;
}

void Matrix33::RotateX(Matrix33 &mtx, float rad)
{
	float s = sin(rad);
	float c = cos(rad);
	memset(mtx.data, 0, sizeof(mtx.data));
	mtx.data[0] = 1;
	mtx.data[4] = c;
	mtx.data[5] = -s;
	mtx.data[7] = s;
	mtx.data[8] = c;
}
void Matrix33::RotateY(Matrix33 &mtx, float rad)
{
	float s = sin(rad);
	float c = cos(rad);
	memset(mtx.data, 0, sizeof(mtx.data));
	mtx.data[0] = c;
	mtx.data[2] = s;
	mtx.data[4] = 1;
	mtx.data[6] = -s;
	mtx.data[8] = c;
}

void Matrix33::Multiply(const Matrix33 &a, const Matrix33 &b, Matrix33 &result)
{
	MatrixMul(3, a.data, b.data, result.data);
}

void Matrix33::Multiply(const Matrix33 &a, const float vec[3], float result[3])
{
	for (int i = 0; i < 3; ++i)
	{
		result[i] = 0;

		for (int k = 0; k < 3; ++k)
		{
			result[i] += a.data[i * 3 + k] * vec[k];
		}
	}
}

void Matrix44::LoadIdentity(Matrix44 &mtx)
{
	memset(mtx.data, 0, sizeof(mtx.data));
	mtx.data[0] = 1.0f;
	mtx.data[5] = 1.0f;
	mtx.data[10] = 1.0f;
	mtx.data[15] = 1.0f;
}

void Matrix44::LoadMatrix33(Matrix44 &mtx, const Matrix33 &m33)
{
	for (int i = 0; i < 3; ++i)
	{
		for (int j = 0; j < 3; ++j)
		{
			mtx.data[i * 4 + j] = m33.data[i * 3 + j];
		}
	}

	for (int i = 0; i < 3; ++i)
	{
		mtx.data[i * 4 + 3] = 0;
		mtx.data[i + 12] = 0;
	}
	mtx.data[15] = 1.0f;
}

void Matrix44::Set(Matrix44 &mtx, const float mtxArray[16])
{
	for(int i = 0; i < 16; ++i)
	{
		mtx.data[i] = mtxArray[i];
	}
}

void Matrix44::Translate(Matrix44 &mtx, const float vec[3])
{
	LoadIdentity(mtx);
	mtx.data[3] = vec[0];
	mtx.data[7] = vec[1];
	mtx.data[11] = vec[2];
}

inline void MatrixMul4(const float *a, const float *b, float *result)
{
	const __m128 b0 = _mm_load_ps(b + 0);
	const __m128 b1 = _mm_load_ps(b + 4);
	const __m128 b2 = _mm_load_ps(b + 8);
	const __m128 b3 = _mm_load_ps(b + 12);

	for (int i = 0; i < 4; ++i) {
		__m128 a_ = _mm_load_ps(a + i*4);

		__m128 a0 = _mm_shuffle_ps(a_, a_, _MM_SHUFFLE(0, 0, 0, 0));
		__m128 a1 = _mm_shuffle_ps(a_, a_, _MM_SHUFFLE(1, 1, 1, 1));
		__m128 a2 = _mm_shuffle_ps(a_, a_, _MM_SHUFFLE(2, 2, 2, 2));
		__m128 a3 = _mm_shuffle_ps(a_, a_, _MM_SHUFFLE(3, 3, 3, 3));

		a0 = _mm_mul_ps(a0, b0);
		a1 = _mm_mul_ps(a1, b1);
		a2 = _mm_mul_ps(a2, b2);
		a3 = _mm_mul_ps(a3, b3);

		_mm_store_ps(result + i*4, _mm_add_ps(_mm_add_ps(a0, a1), _mm_add_ps(a2, a3)));
	}
}

void Matrix44::Multiply(const Matrix44 &a, const Matrix44 &b, Matrix44 &result)
{
	MatrixMul4(a.data, b.data, result.data);
}

