#pragma once

#ifdef _SYSTEM
#define _SYSTEM_EXP __declspec(dllexport)
#else
#define _SYSTEM_EXP __declspec(dllimport)
#endif // _SYSTEM

#define IN
#define OUT
