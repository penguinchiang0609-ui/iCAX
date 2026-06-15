#pragma once

#ifdef _RESOURCE_POOL
#define _RESOURCE_POOL_EXP __declspec(dllexport)
#else
#define _RESOURCE_POOL_EXP __declspec(dllimport)
#endif

#define IN
#define OUT
