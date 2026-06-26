#pragma once

#ifdef _PRODUCT
#define _PRODUCT_EXP __declspec(dllexport)
#else
#define _PRODUCT_EXP __declspec(dllimport)
#endif

#define IN
#define OUT
