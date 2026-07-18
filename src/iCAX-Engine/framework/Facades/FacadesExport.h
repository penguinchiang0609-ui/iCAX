#pragma once

#ifdef _FACADES
#define _FACADES_EXP __declspec(dllexport)
#else
#define _FACADES_EXP __declspec(dllimport)
#endif

#define IN
#define OUT
