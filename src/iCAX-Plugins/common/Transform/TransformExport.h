#pragma once

#ifdef _TRANSFORM
#define _TRANSFORM_EXP __declspec(dllexport)
#else
#define _TRANSFORM_EXP __declspec(dllimport)
#endif

