#pragma once

#ifdef _PRODUCT_CONTEXT
#define _PRODUCT_CONTEXT_EXP __declspec(dllexport)
#else
#define _PRODUCT_CONTEXT_EXP __declspec(dllimport)
#endif

#define IN
#define OUT
