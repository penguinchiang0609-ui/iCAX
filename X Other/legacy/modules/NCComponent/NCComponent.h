#pragma once

#ifdef _GCODECORE
#define _GCODECORE_EXP __declspec(dllexport)
#else
#define _GCODECORE_EXP __declspec(dllimport)
#endif // _DATABASE

#define IN
#define OUT
