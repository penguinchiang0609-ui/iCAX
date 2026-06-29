#pragma once

#ifdef _COLLIDER_DATA
#define _COLLIDER_DATA_EXP __declspec(dllexport)
#else
#define _COLLIDER_DATA_EXP __declspec(dllimport)
#endif

#define IN
#define OUT
