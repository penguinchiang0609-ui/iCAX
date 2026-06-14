#pragma once

#ifdef _PC
#define _PC_EXP __declspec(dllexport)
#else
#define _PC_EXP __declspec(dllimport)
#endif // _DATABASE

#define IN
#define OUT
