#pragma once

#ifdef _NCDATA
#define _NCDATA_EXP __declspec(dllexport)
#else
#define _NCDATA_EXP __declspec(dllimport)
#endif // _DATABASE

#define IN
#define OUT