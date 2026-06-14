#pragma once

#include <cmath>

#ifdef _DATA
#define _DATA_EXP __declspec(dllexport)
#else
#define _DATA_EXP __declspec(dllimport)
#endif // _DATA


#define IN
#define OUT

#define EPSILON 1E-5
#define ALIGNED_BITS 8

#define DOUBLE_EQL(a,b) (std::fabs((a) - (b)) <= EPSILON)

#define ZERO_EQL(a) (std::fabs(a) <= EPSILON)

#ifndef M_PI
#define M_PI 3.141592653589793
#endif

#ifndef M_PI_2
#define M_PI_2 (3.141592653589793 / 2)
#endif

